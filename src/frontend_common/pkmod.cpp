// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend_common/pkmod.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include <fmt/format.h>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "frontend_common/data_manager.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace PkMod {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Minimal TOML parser — just enough for mod.toml manifests.
// We avoid pulling in a full TOML library; the format is simple key=value
// with [[settings]] array-of-tables.
// ---------------------------------------------------------------------------

static std::string Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return std::string(s);
}

static std::string Unquote(std::string_view s) {
    s = Trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return std::string(s.substr(1, s.size() - 2));
    }
    return std::string(s);
}

static std::vector<std::string> ParseArray(std::string_view value) {
    std::vector<std::string> result;
    // Remove brackets
    auto start = value.find('[');
    auto end = value.rfind(']');
    if (start == std::string_view::npos || end == std::string_view::npos)
        return result;
    value = value.substr(start + 1, end - start - 1);
    // Split by comma
    std::string current;
    for (char c : value) {
        if (c == ',') {
            auto item = Unquote(Trim(current));
            if (!item.empty())
                result.push_back(std::move(item));
            current.clear();
        } else {
            current += c;
        }
    }
    auto item = Unquote(Trim(current));
    if (!item.empty())
        result.push_back(std::move(item));
    return result;
}

std::optional<Manifest> ParseManifest(const fs::path& toml_path) {
    std::ifstream file(toml_path);
    if (!file.is_open()) {
        LOG_ERROR(Frontend, "pkmod: Failed to open manifest: {}", toml_path.string());
        return std::nullopt;
    }

    Manifest manifest;
    Setting* current_setting = nullptr;
    std::string line;

    while (std::getline(file, line)) {
        // Strip \r if present
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        // Check for [[settings]] table
        if (trimmed == "[[settings]]") {
            manifest.settings.emplace_back();
            current_setting = &manifest.settings.back();
            continue;
        }

        // Parse key = value
        auto eq = trimmed.find('=');
        if (eq == std::string::npos)
            continue;

        auto key = Trim(trimmed.substr(0, eq));
        auto raw_value = Trim(trimmed.substr(eq + 1));
        auto value = Unquote(raw_value);

        if (current_setting) {
            // We're inside a [[settings]] block
            if (key == "key")
                current_setting->key = value;
            else if (key == "type")
                current_setting->type = value;
            else if (key == "default")
                current_setting->default_value = value;
            else if (key == "label")
                current_setting->label = value;
            else if (key == "restart")
                current_setting->restart = (value == "true");
        } else {
            // Top-level keys
            if (key == "id")
                manifest.id = value;
            else if (key == "name")
                manifest.name = value;
            else if (key == "version")
                manifest.version = value;
            else if (key == "description")
                manifest.description = value;
            else if (key == "license")
                manifest.license = value;
            else if (key == "loader")
                manifest.loader = value;
            else if (key == "affects")
                manifest.affects = value;
            else if (key == "authors")
                manifest.authors = ParseArray(raw_value);
            else if (key == "game")
                manifest.game_versions = ParseArray(raw_value);
        }
    }

    if (manifest.id.empty()) {
        LOG_ERROR(Frontend, "pkmod: Manifest has no 'id' field: {}", toml_path.string());
        return std::nullopt;
    }

    return manifest;
}

// ---------------------------------------------------------------------------
// Install / scan
// ---------------------------------------------------------------------------

fs::path GetPkModDir(u64 program_id) {
    return FrontendCommon::DataManager::GetDataDir(FrontendCommon::DataManager::DataDir::Mods) /
           fmt::format("{:016X}", program_id) / "pkmods";
}

std::optional<InstalledMod> InstallPkMod(const fs::path& pkmod_path, u64 program_id) {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1. Create a temp directory and extract the ZIP
    const auto tmp_dir = fs::temp_directory_path(ec) / "phoenix" / "pkmod_install";
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(tmp_dir, ec);

    // Use the system's minizip / zlib to extract (simple approach via std::system on Windows)
    // We'll do a manual ZIP extraction using the C++ std library
    // Actually, we can use QuaZip through Qt, but since this is in frontend_common (non-Qt),
    // let's do a filesystem-level extraction.

    // Copy the .pkmod to .zip and use PowerShell / tar to extract
    const auto zip_copy = tmp_dir / "mod.zip";
    fs::copy_file(pkmod_path, zip_copy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_ERROR(Frontend, "pkmod: Failed to copy archive: {}", ec.message());
        return std::nullopt;
    }

#ifdef _WIN32
    // Use PowerShell to extract
    const std::string cmd = fmt::format(
        "powershell -NoProfile -Command \"Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "[System.IO.Compression.ZipFile]::ExtractToDirectory('{}', '{}')\"",
        zip_copy.string(), tmp_dir.string());
    const int ret = std::system(cmd.c_str());
#else
    const std::string cmd =
        fmt::format("unzip -o -q '{}' -d '{}'", zip_copy.string(), tmp_dir.string());
    const int ret = std::system(cmd.c_str());
#endif
    if (ret != 0) {
        LOG_ERROR(Frontend, "pkmod: Failed to extract archive (exit code {})", ret);
        return std::nullopt;
    }

    // 2. Parse the manifest
    const auto manifest_path = tmp_dir / "mod.toml";
    if (!fs::exists(manifest_path, ec)) {
        LOG_ERROR(Frontend, "pkmod: No mod.toml found in archive");
        return std::nullopt;
    }

    auto manifest = ParseManifest(manifest_path);
    if (!manifest) {
        return std::nullopt;
    }

    // 3. Determine install directory
    const auto install_dir = GetPkModDir(program_id) / manifest->id;
    fs::remove_all(install_dir, ec);
    fs::create_directories(install_dir, ec);

    // 4. Copy everything from tmp to install dir
    fs::copy(tmp_dir, install_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_ERROR(Frontend, "pkmod: Failed to copy mod files: {}", ec.message());
        return std::nullopt;
    }

    // 5. Find the native library
    InstalledMod installed;
    installed.manifest = std::move(*manifest);
    installed.install_dir = install_dir;
    installed.enabled = true;

#ifdef _WIN32
    const auto dll_path = install_dir / "bin" / "windows-x64" / "mod.dll";
#elif defined(__linux__)
    const auto dll_path = install_dir / "bin" / "linux-x64" / "mod.so";
#else
    const auto dll_path = install_dir / "bin" / "mod.dylib";
#endif
    if (fs::exists(dll_path, ec)) {
        installed.native_library = dll_path;
    }

    // 6. Cleanup temp
    fs::remove_all(tmp_dir, ec);

    LOG_INFO(Frontend, "pkmod: Installed '{}' v{} to {}",
             installed.manifest.name, installed.manifest.version,
             install_dir.string());

    return installed;
}

std::vector<InstalledMod> ScanInstalledPkMods(u64 program_id) {
    std::vector<InstalledMod> result;
    const auto pkmod_dir = GetPkModDir(program_id);
    std::error_code ec;

    if (!fs::exists(pkmod_dir, ec)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(pkmod_dir, ec)) {
        if (!entry.is_directory(ec))
            continue;

        auto dir_name = entry.path().filename().string();
        bool enabled = true;
        if (dir_name.ends_with(".disabled")) {
            enabled = false;
        }

        const auto manifest_path = entry.path() / "mod.toml";
        if (!fs::exists(manifest_path, ec))
            continue;

        auto manifest = ParseManifest(manifest_path);
        if (!manifest)
            continue;

        InstalledMod mod;
        mod.manifest = std::move(*manifest);
        mod.install_dir = entry.path();
        mod.enabled = enabled;

#ifdef _WIN32
        const auto dll_path = entry.path() / "bin" / "windows-x64" / "mod.dll";
#elif defined(__linux__)
        const auto dll_path = entry.path() / "bin" / "linux-x64" / "mod.so";
#else
        const auto dll_path = entry.path() / "bin" / "mod.dylib";
#endif
        if (fs::exists(dll_path, ec)) {
            mod.native_library = dll_path;
        }

        result.push_back(std::move(mod));
    }

    return result;
}

bool SetPkModEnabled(InstalledMod& mod, bool enabled) {
    std::error_code ec;
    auto current = mod.install_dir;
    auto name = current.filename().string();

    if (enabled && name.ends_with(".disabled")) {
        auto new_path = current.parent_path() / name.substr(0, name.length() - 9);
        fs::rename(current, new_path, ec);
        if (!ec) {
            mod.install_dir = new_path;
            mod.enabled = true;
            return true;
        }
    } else if (!enabled && !name.ends_with(".disabled")) {
        auto new_path = current.parent_path() / (name + ".disabled");
        fs::rename(current, new_path, ec);
        if (!ec) {
            mod.install_dir = new_path;
            mod.enabled = false;
            return true;
        }
    }
    return !ec;
}

// ---------------------------------------------------------------------------
// API stubs – these are called by the mod DLL through the vtable.
// They're no-ops for now; real implementations can be added later.
// ---------------------------------------------------------------------------

static int StubRegisterEvent(void* /*ctx*/, const char* event_name, void* /*cb*/, void* /*user*/) {
    LOG_DEBUG(Frontend, "pkmod: register_event('{}') – stub", event_name ? event_name : "");
    return 0; // "handle"
}

static void StubUnregisterEvent(void* /*ctx*/, int /*handle*/) {}

static int StubRegisterHook(void* /*ctx*/, const char* hook_name, void* /*cb*/, void* /*user*/) {
    LOG_DEBUG(Frontend, "pkmod: register_hook('{}') – stub", hook_name ? hook_name : "");
    return 0;
}

static void StubUnregisterHook(void* /*ctx*/, int /*handle*/) {}

static const char* StubGetSetting(void* /*ctx*/, const char* key) {
    LOG_DEBUG(Frontend, "pkmod: get_setting('{}') – stub", key ? key : "");
    return "";
}

static void StubSetStatus(void* /*ctx*/, const char* key, const char* value) {
    LOG_DEBUG(Frontend, "pkmod: set_status('{}', '{}') – stub", key ? key : "", value ? value : "");
}

// ---------------------------------------------------------------------------
// PkModLoader
// ---------------------------------------------------------------------------

PkModLoader::PkModLoader() {
    // Wire up the API vtable stubs.
    api_.register_event = reinterpret_cast<void*>(&StubRegisterEvent);
    api_.unregister_event = reinterpret_cast<void*>(&StubUnregisterEvent);
    api_.register_hook = reinterpret_cast<void*>(&StubRegisterHook);
    api_.unregister_hook = reinterpret_cast<void*>(&StubUnregisterHook);
    api_.get_setting = reinterpret_cast<void*>(&StubGetSetting);
    api_.set_status = reinterpret_cast<void*>(&StubSetStatus);
}

PkModLoader::~PkModLoader() {
    UnloadAll();
}

void PkModLoader::LoadAll(u64 program_id) {
    UnloadAll();
    current_program_id_ = program_id;

    auto mods = ScanInstalledPkMods(program_id);
    for (const auto& mod : mods) {
        if (!mod.enabled) {
            LOG_INFO(Frontend, "pkmod: Skipping disabled mod '{}'", mod.manifest.id);
            continue;
        }
        if (mod.native_library.empty()) {
            LOG_INFO(Frontend, "pkmod: Mod '{}' has no native library, skipping DLL load",
                     mod.manifest.id);
            continue;
        }
        LoadSingle(mod);
    }

    LOG_INFO(Frontend, "pkmod: Loaded {} native mod(s) for program {:016X}",
             loaded_mods_.size(), program_id);
}

void PkModLoader::UnloadAll() {
    for (auto& mod : loaded_mods_) {
        UnloadSingle(mod);
    }
    loaded_mods_.clear();
    current_program_id_ = 0;
}

bool PkModLoader::LoadSingle(const InstalledMod& mod) {
    LOG_INFO(Frontend, "pkmod: Loading native library for '{}': {}",
             mod.manifest.id, mod.native_library.string());

#ifdef _WIN32
    HMODULE handle = LoadLibraryW(mod.native_library.c_str());
    if (!handle) {
        LOG_ERROR(Frontend, "pkmod: Failed to load DLL '{}' (error {})",
                  mod.native_library.string(), GetLastError());
        return false;
    }
#else
    void* handle = dlopen(mod.native_library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        LOG_ERROR(Frontend, "pkmod: Failed to load SO '{}': {}",
                  mod.native_library.string(), dlerror());
        return false;
    }
#endif

    // Check API version
#ifdef _WIN32
    auto* version_ptr = reinterpret_cast<u32*>(
        GetProcAddress(handle, "modtopia_api_version"));
#else
    auto* version_ptr = reinterpret_cast<u32*>(dlsym(handle, "modtopia_api_version"));
#endif

    if (version_ptr) {
        const u32 mod_version = *version_ptr;
        if (mod_version != kPkModApiVersion) {
            LOG_WARNING(Frontend,
                        "pkmod: Mod '{}' has API version {} (expected {}), loading anyway",
                        mod.manifest.id, mod_version, kPkModApiVersion);
        }
    } else {
        LOG_WARNING(Frontend, "pkmod: Mod '{}' does not export 'modtopia_api_version'",
                    mod.manifest.id);
    }

    // Look up mod_init
    using ModInitFn = int (*)(const void*, void*);
#ifdef _WIN32
    auto mod_init = reinterpret_cast<ModInitFn>(GetProcAddress(handle, "mod_init"));
#else
    auto mod_init = reinterpret_cast<ModInitFn>(dlsym(handle, "mod_init"));
#endif

    if (mod_init) {
        LOG_INFO(Frontend, "pkmod: Calling mod_init for '{}'", mod.manifest.id);
        // Pass the API vtable and a context pointer (nullptr for now;
        // a real implementation would provide emulator state).
        const int result = mod_init(&api_, nullptr);
        if (result != 0) {
            LOG_WARNING(Frontend, "pkmod: mod_init for '{}' returned {}", mod.manifest.id, result);
        }
    } else {
        LOG_WARNING(Frontend, "pkmod: Mod '{}' does not export 'mod_init'", mod.manifest.id);
    }

    LoadedPkMod loaded;
    loaded.mod_id = mod.manifest.id;
    loaded.dll_path = mod.native_library;
    loaded.handle = reinterpret_cast<void*>(handle);
    loaded_mods_.push_back(std::move(loaded));

    LOG_INFO(Frontend, "pkmod: Successfully loaded '{}'", mod.manifest.id);
    return true;
}

void PkModLoader::UnloadSingle(LoadedPkMod& mod) {
    if (!mod.handle) {
        return;
    }

    LOG_INFO(Frontend, "pkmod: Unloading '{}'", mod.mod_id);

    // Try calling mod_shutdown if it exists
    using ModShutdownFn = void (*)();
#ifdef _WIN32
    auto mod_shutdown = reinterpret_cast<ModShutdownFn>(
        GetProcAddress(static_cast<HMODULE>(mod.handle), "mod_shutdown"));
    if (mod_shutdown) {
        mod_shutdown();
    }
    FreeLibrary(static_cast<HMODULE>(mod.handle));
#else
    auto mod_shutdown = reinterpret_cast<ModShutdownFn>(dlsym(mod.handle, "mod_shutdown"));
    if (mod_shutdown) {
        mod_shutdown();
    }
    dlclose(mod.handle);
#endif

    mod.handle = nullptr;
}

} // namespace PkMod
