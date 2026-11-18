// SPDX-FileCopyrightText: Copyright 2026 PhoenixEmu contributors
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#if defined(_WIN32) || defined(__linux__)
#define PHOENIX_FFX_RUNTIME 1
#endif

#ifdef PHOENIX_FFX_RUNTIME
#ifndef _WIN32
// ffx_api.h marks its declarations __declspec(dllexport); only its types are used here.
#define __declspec(x)
#include "api/include/ffx_api.h"
#undef __declspec
#else
#include "api/include/ffx_api.h"
#endif
#endif

namespace Vulkan::FfxRuntime {

#ifdef PHOENIX_FFX_RUNTIME
/// Where the FidelityFX API comes from.
enum class Source {
    Amd,    ///< AMD's runtime: amd_fidelityfx_loader_vk.dll next to the emulator (Windows)
    Fsr4Vk, ///< fsr4-vk (github.com/Astyyyyy/fsr4-vk), shipped in fsr4-vk/ next to the emulator
};

/// The FidelityFX API of one runtime.
struct Api {
    PfnFfxCreateContext create_context{};
    PfnFfxDestroyContext destroy_context{};
    PfnFfxConfigure configure{};
    PfnFfxQuery query{};
    PfnFfxDispatch dispatch{};
    Source source{};
};

/// Loads a runtime, or shares the one loaded. AMD's unloads (with the upscaler and frame
/// generation DLLs the loader brought in) when the last holder lets go, so the next game loads it
/// afresh: kept loaded, frame generation kept the stopped game's device and the next game crashed.
/// Holders destroy their contexts first. Null when the library or its exports are missing.
[[nodiscard]] std::shared_ptr<const Api> Acquire(Source source = Source::Amd);

[[nodiscard]] const char* SourceName(Source source);

/// Whether AMD's frame generation DLL is still in the process (nothing has unloaded it).
[[nodiscard]] bool FrameGenerationLoaded();

/// The runtime's errors and warnings about what a context was given, into the log (ffxApiMessage).
void LogMessage(uint32_t type, const wchar_t* message);
#endif

} // namespace Vulkan::FfxRuntime
