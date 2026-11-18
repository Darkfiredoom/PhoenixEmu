// SPDX-FileCopyrightText: Copyright 2026 PhoenixEmu contributors
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/renderer_vulkan/vk_ffx_runtime.h"

#ifdef PHOENIX_FFX_RUNTIME

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "common/common_types.h"
#include "common/logging.h"

namespace Vulkan::FfxRuntime {

namespace {

#ifdef _WIN32
using Module = HMODULE;
constexpr const wchar_t* AMD_LOADER = L"amd_fidelityfx_loader_vk.dll";
// AMD's loader loads these beside itself; they go with it.
constexpr const wchar_t* AMD_PROVIDERS[] = {L"amd_fidelityfx_framegeneration_vk.dll",
                                            L"amd_fidelityfx_upscaler_vk.dll"};
// fsr4-vk builds as amd_fidelityfx_upscaler_vk.dll; it is shipped renamed, in its own folder, so
// AMD's loader never takes it for its own upscaler.
constexpr const char* FSR4_VK_LIBRARY = "phoenix_fsr4_vk.dll";

std::filesystem::path ExecutableDir() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(std::wstring(path, length)).parent_path();
}
Module Open(const std::filesystem::path& path) {
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}
void* Symbol(Module module, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(module, name));
}
void Close(Module module) {
    FreeLibrary(module);
}
#else
using Module = void*;
constexpr const char* FSR4_VK_LIBRARY = "libphoenix_fsr4_vk.so";

std::filesystem::path ExecutableDir() {
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error).parent_path();
}
Module Open(const std::filesystem::path& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* Symbol(Module module, const char* name) {
    return dlsym(module, name);
}
void Close(Module module) {
    dlclose(module);
}
#endif

struct Loaded : Api {
    Module module{};
};

std::mutex mutex;
std::weak_ptr<const Loaded> current_amd;
std::weak_ptr<const Loaded> current_fsr4_vk;

void Unload(const Loaded* loaded) {
    std::scoped_lock lock{mutex};
    Close(loaded->module);
#ifdef _WIN32
    if (loaded->source == Source::Amd) {
        for (const wchar_t* name : AMD_PROVIDERS) {
            for (int i = 0; i < 8; ++i) {
                const HMODULE module = GetModuleHandleW(name);
                if (!module || !FreeLibrary(module)) {
                    break;
                }
            }
        }
    }
#endif
    LOG_INFO(Render_Vulkan, "FidelityFX: {} runtime unloaded{}", SourceName(loaded->source),
             FrameGenerationLoaded() ? " (frame generation DLL still loaded)" : "");
    delete loaded;
}

Module OpenRuntime(Source source) {
    if (source == Source::Fsr4Vk) {
        const auto path = ExecutableDir() / "fsr4-vk" / FSR4_VK_LIBRARY;
        const Module module = Open(path);
        if (!module) {
#ifdef _WIN32
            const std::string reason = std::to_string(GetLastError());
#else
            const char* const error = dlerror();
            const std::string reason = error ? error : "unknown";
#endif
            LOG_INFO(Render_Vulkan, "FidelityFX: fsr4-vk not loaded from {} ({})", path.string(),
                     reason);
        }
        return module;
    }
#ifdef _WIN32
    const Module module = Open(ExecutableDir() / AMD_LOADER);
    if (!module) {
        LOG_INFO(Render_Vulkan, "AMD FidelityFX: amd_fidelityfx_loader_vk.dll not found next to "
                                "the emulator");
    }
    return module;
#else
    return nullptr; // AMD ships its runtime for Windows only
#endif
}

} // Anonymous namespace

const char* SourceName(Source source) {
    return source == Source::Fsr4Vk ? "fsr4-vk" : "AMD";
}

std::shared_ptr<const Api> Acquire(Source source) {
    std::scoped_lock lock{mutex};
    auto& current = source == Source::Fsr4Vk ? current_fsr4_vk : current_amd;
    if (auto existing = current.lock()) {
        return existing;
    }
    const Module module = OpenRuntime(source);
    if (!module) {
        return nullptr;
    }
    auto* const loaded = new Loaded{};
    loaded->module = module;
    loaded->source = source;
    loaded->create_context = reinterpret_cast<PfnFfxCreateContext>(Symbol(module, "ffxCreateContext"));
    loaded->destroy_context =
        reinterpret_cast<PfnFfxDestroyContext>(Symbol(module, "ffxDestroyContext"));
    loaded->configure = reinterpret_cast<PfnFfxConfigure>(Symbol(module, "ffxConfigure"));
    loaded->query = reinterpret_cast<PfnFfxQuery>(Symbol(module, "ffxQuery"));
    loaded->dispatch = reinterpret_cast<PfnFfxDispatch>(Symbol(module, "ffxDispatch"));
    if (!loaded->create_context || !loaded->destroy_context || !loaded->configure ||
        !loaded->query || !loaded->dispatch) {
        LOG_WARNING(Render_Vulkan, "FidelityFX: the {} runtime lacks its exports",
                    SourceName(source));
        Close(module);
        delete loaded;
        return nullptr;
    }
    LOG_INFO(Render_Vulkan, "FidelityFX: {} runtime loaded", SourceName(source));
    // The deleter takes the lock itself: release happens outside it.
    std::shared_ptr<const Loaded> shared{loaded, [](const Loaded* l) { Unload(l); }};
    current = shared;
    return shared;
}

void LogMessage(uint32_t type, const wchar_t* message) {
    // The first 48, then every 500th: a bad input repeats every frame.
    static std::atomic<u32> count{};
    const u32 n = count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 48 && n % 500 != 0) {
        return;
    }
    std::string text;
    for (const wchar_t* c = message; c != nullptr && *c != 0; ++c) {
        text.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    if (type == FFX_API_MESSAGE_TYPE_ERROR) {
        LOG_ERROR(Render_Vulkan, "FidelityFX runtime: {} (message {})", text, n);
    } else {
        LOG_WARNING(Render_Vulkan, "FidelityFX runtime: {} (message {})", text, n);
    }
}

bool FrameGenerationLoaded() {
#ifdef _WIN32
    return GetModuleHandleW(AMD_PROVIDERS[0]) != nullptr;
#else
    return false;
#endif
}

} // namespace Vulkan::FfxRuntime

#endif // PHOENIX_FFX_RUNTIME
