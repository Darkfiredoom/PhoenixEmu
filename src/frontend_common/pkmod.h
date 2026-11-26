// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"

namespace PkMod {

/// A single configurable setting declared in the mod.toml manifest.
struct Setting {
    std::string key;
    std::string type;   // "bool", "text", "int"
    std::string default_value;
    std::string label;
    bool restart = false;
};

/// Parsed contents of a mod.toml manifest inside a .pkmod archive.
struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string license;
    std::string loader;        // semver range, e.g. ">=0.2.0 <1.0.0"
    std::string affects;       // "visual", "gameplay", etc.
    std::vector<std::string> authors;
    std::vector<std::string> game_versions;
    std::vector<Setting> settings;
};

/// An installed pkmod on disk.
struct InstalledMod {
    Manifest manifest;
    std::filesystem::path install_dir;      // where the mod was extracted
    std::filesystem::path native_library;   // path to mod.dll / mod.so
    bool enabled = true;
};

/// Parse a mod.toml file into a Manifest.  Returns std::nullopt on failure.
[[nodiscard]] std::optional<Manifest> ParseManifest(const std::filesystem::path& toml_path);

/// Install a .pkmod file for the given program_id.
/// Extracts the ZIP, parses mod.toml, copies native binaries.
/// Returns the installed mod info, or std::nullopt on failure.
[[nodiscard]] std::optional<InstalledMod> InstallPkMod(
    const std::filesystem::path& pkmod_path, u64 program_id);

/// Scan the pkmod subdirectory for installed pkmods for a given program.
[[nodiscard]] std::vector<InstalledMod> ScanInstalledPkMods(u64 program_id);

/// Enable or disable an installed pkmod by renaming its directory.
bool SetPkModEnabled(InstalledMod& mod, bool enabled);

/// Get the pkmod install directory for a program.
[[nodiscard]] std::filesystem::path GetPkModDir(u64 program_id);

// ---------------------------------------------------------------------------
// Runtime DLL loading
// ---------------------------------------------------------------------------

/// Required API version for native pkmod plugins.
constexpr u32 kPkModApiVersion = 6;

/// Minimal API vtable passed to mod_init.
/// Function-pointer slots mirror the offsets the reference discord_status mod expects.
/// Slots the emulator doesn't implement yet are set to no-op stubs so the DLL
/// can initialise without crashing.
struct PkModApi {
    // Pad to 0xA0 / 160 bytes → 20 pointers on 64-bit.
    void* _reserved[20]{};
    // +0xA0: register_event(ctx, event_name, callback, user)
    void* register_event{};
    // +0xA8: unregister_event(ctx, handle)
    void* unregister_event{};
    // +0xB0: register_hook(ctx, hook_name, callback, user)
    void* register_hook{};
    // +0xB8: unregister_hook(ctx, handle)
    void* unregister_hook{};
    // Pad to 0xD0 → 4 more pointers
    void* _reserved2[4]{};
    // +0xD0: get_setting(ctx, key) → const char*
    void* get_setting{};
    // +0xD8: set_status(ctx, key, value)
    void* set_status{};
};

/// Handle for a single loaded pkmod native library.
struct LoadedPkMod {
    std::string mod_id;
    std::filesystem::path dll_path;
#ifdef _WIN32
    void* handle{};  // HMODULE
#else
    void* handle{};  // dlopen handle
#endif
};

/// Manages loading / unloading of pkmod native libraries for one game session.
class PkModLoader {
public:
    PkModLoader();
    ~PkModLoader();

    /// Load all enabled pkmods with native libraries for the given program.
    /// Call this after the emulation thread has started.
    void LoadAll(u64 program_id);

    /// Unload all loaded pkmods. Call this on game shutdown.
    void UnloadAll();

    /// Returns the list of currently loaded pkmods.
    [[nodiscard]] const std::vector<LoadedPkMod>& GetLoaded() const { return loaded_mods_; }

private:
    bool LoadSingle(const InstalledMod& mod);
    void UnloadSingle(LoadedPkMod& mod);

    PkModApi api_{};
    std::vector<LoadedPkMod> loaded_mods_;
    u64 current_program_id_{};
};

} // namespace PkMod
