// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <vector>
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <qdir.h>
#include "common/common_types.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "qt_common/config/qt_config.h"

using Settings::Category;
using Settings::ConfirmStop;
using Settings::Setting;
using Settings::SwitchableSetting;

#ifndef CANNOT_EXPLICITLY_INSTANTIATE
namespace Settings {
extern template class Setting<bool>;
extern template class Setting<std::string>;
extern template class Setting<u16, true>;
extern template class Setting<u32>;
extern template class Setting<u8, true>;
extern template class Setting<u8>;
extern template class Setting<unsigned long long>;
} // namespace Settings
#endif

namespace UISettings {

bool IsDarkTheme();

struct ContextualShortcut {
    std::string keyseq;
    std::string controller_keyseq;
    int context;
    bool repeat;
};

struct Shortcut {
    std::string name;
    std::string group;
    ContextualShortcut shortcut;
};

enum class Theme {
    Default,
    DefaultColorful,
    Dark,
    DarkColorful,
    MidnightBlue,
    MidnightBlueColorful,
    Phoenix,
};

// PhoenixEmu's own theme everywhere (warm charcoal and ember gold).
static constexpr Theme default_theme{Theme::Phoenix};

using Themes = std::array<std::pair<const char*, const char*>, 7>;
extern const Themes themes;

struct GameDir {
    std::string path;
    bool deep_scan = false;
    bool expanded = false;
    bool operator==(const GameDir& rhs) const {
        return path == rhs.path;
    }
    bool operator!=(const GameDir& rhs) const {
        return !operator==(rhs);
    }
};

// One file in the Amiibo Library and how the player organised it.
struct AmiiboEntry {
    QString path;
    QString name;  ///< The player's own name; empty for the amiibo's name.
    QString group; ///< The player's own group; empty for none.
    bool favorite = false;
};

struct Values {
    Settings::Linkage linkage{1000};

    QByteArray geometry;
    QByteArray state;

    QByteArray renderwindow_geometry;

    QByteArray gamelist_header_state;

    Setting<bool> single_window_mode{linkage, true, "singleWindowMode", Category::Ui};
    Setting<bool> fullscreen{linkage, false, "fullscreen", Category::Ui};
    Setting<bool> show_filter_bar{linkage, true, "showFilterBar", Category::Ui};
    Setting<bool> show_status_bar{linkage, true, "showStatusBar", Category::Ui};

    SwitchableSetting<ConfirmStop> confirm_before_stopping{linkage,
                                                           ConfirmStop::Ask_Always,
                                                           "confirmStop",
                                                           Category::UiGeneral,
                                                           Settings::Specialization::Default,
                                                           true,
                                                           true};

    Setting<bool> first_start{linkage, true, "firstStart", Category::Ui};
    Setting<bool> pause_when_in_background{linkage,
                                           false,
                                           "pauseWhenInBackground",
                                           Category::UiGeneral,
                                           Settings::Specialization::Default,
                                           true,
                                           true};
    Setting<bool> mute_when_in_background{linkage,
                                          false,
                                          "muteWhenInBackground",
                                          Category::UiAudio,
                                          Settings::Specialization::Default,
                                          true,
                                          true};
    Setting<bool> hide_mouse{
        linkage, true, "hideInactiveMouse", Category::UiGeneral, Settings::Specialization::Default,
        true,    true};
    // Set when Vulkan is known to crash the application
    bool has_broken_vulkan = false;

    Setting<bool> select_user_on_boot{linkage,
                                      false,
                                      "select_user_on_boot",
                                      Category::UiGeneral,
                                      Settings::Specialization::Default,
                                      true,
                                      true};

    Setting<bool> check_for_updates{linkage, true, "check_for_updates", Category::UiGeneral};
    // Release tag the user chose to skip, and the epoch second before which updates stay quiet.
    Setting<std::string> skipped_update_tag{linkage, {}, "skipped_update_tag", Category::Ui};
    /// The release build (build-N) last started: a different one means an update was installed,
    /// and its "What's new" is shown once.
    Setting<std::string> last_seen_build{linkage, {}, "last_seen_build", Category::Ui};
    Setting<unsigned long long> update_remind_at{linkage, 0, "update_remind_at", Category::Ui};

    // Linux/MinGW may support (requires libdl support)
    SwitchableSetting<bool> enable_gamemode{linkage,
#ifndef _MSC_VER
                                            true,
#else
                                            false,
#endif
                                            "enable_gamemode", Category::UiGeneral};
#ifdef __unix__
    SwitchableSetting<bool> gui_force_x11{linkage, false, "gui_force_x11", Category::UiGeneral};
    Setting<bool> gui_hide_backend_warning{linkage, false, "gui_hide_backend_warning",
                                           Category::UiGeneral};
#endif

    // Discord RPC
    Setting<bool> enable_discord_presence{linkage, true, "enable_discord_presence", Category::Ui};

    // Menu sounds (phoenix/util/menu_sounds.cpp): the click and start chime, the home screen hum
    // and their volume.
    Setting<bool> menu_sounds{linkage, true, "menu_sounds", Category::Ui};
    Setting<bool> menu_ambient{linkage, true, "menu_ambient", Category::Ui};
    /// The picked game's own title music (from the player's copy) on the home screen and
    /// while it loads.
    Setting<bool> menu_game_music{linkage, true, "menu_game_music", Category::Ui};
    /// That music: random songs from the game's soundtrack (true), or its title theme.
    Setting<bool> menu_music_random{linkage, true, "menu_music_random", Category::Ui};
    Setting<u32, true> menu_sound_volume{linkage, 60, 0, 100, "menu_sound_volume", Category::Ui};
    /// The home screen's speaker button: every menu sound silent (music, hum, clicks, chime).
    Setting<bool> menu_muted{linkage, false, "menu_muted", Category::Ui};

    // logging
    Setting<bool> show_console{linkage, false, "showConsole", Category::Ui};

    // Screenshots
    Setting<bool> enable_screenshot_save_as{linkage, true, "enable_screenshot_save_as",
                                            Category::Screenshots};
    Setting<u32> screenshot_height{linkage, 0, "screenshot_height", Category::Screenshots};

    std::string roms_path;
    std::string game_dir_deprecated;
    bool game_dir_deprecated_deepscan;
    QVector<GameDir> game_dirs;
    QStringList recent_files;
    // Amiibo files added to the Amiibo Library, most recently tapped first.
    QVector<AmiiboEntry> amiibo_library;
    QString amiibo_view; ///< The library's last filter: "", "favorites", "group:<name>" or "series:<name>".
    int amiibo_sort = 0; ///< 0 recently used, 1 name, 2 series.
    Setting<std::string> language{linkage, {}, "language", Category::Paths};

    // Optional key that enables the SteamGridDB icon fallback
    Setting<std::string> steamgriddb_api_key{linkage, {}, "steamgriddb_api_key", Category::Ui};
    /// The player's game case scans (Cases/<game>/..., Banner/<game>.jpg); empty: <user>/case_art.
    Setting<std::string> case_art_folder{linkage, {}, "case_art_folder", Category::UiGameList};
    // Look up pictures online for listed games that have none (SteamGridDB with a key).
    Setting<bool> auto_game_pictures{linkage, true, "auto_game_pictures", Category::Ui};
    // Cloud & LAN Save Sync
    Setting<std::string> save_sync_folder{linkage, {}, "save_sync_folder", Category::Ui};
    Setting<bool> auto_save_sync{linkage, false, "auto_save_sync", Category::Ui};

    std::string theme;

    // Shortcut name <Shortcut, context>
    std::vector<Shortcut> shortcuts;

    Setting<u32> callout_flags{linkage, 0, "calloutFlags", Category::Ui};

    // multiplayer settings
    Setting<std::string> multiplayer_nickname{linkage, {}, "nickname", Category::Multiplayer};
    Setting<std::string> multiplayer_filter_text{linkage, {}, "filter_text", Category::Multiplayer};
    Setting<bool> multiplayer_filter_games_owned{linkage, false, "filter_games_owned",
                                                 Category::Multiplayer};
    Setting<bool> multiplayer_filter_hide_empty{linkage, false, "filter_games_hide_empty",
                                                Category::Multiplayer};
    Setting<bool> multiplayer_filter_hide_full{linkage, false, "filter_games_hide_full",
                                               Category::Multiplayer};
    Setting<std::string> multiplayer_ip{linkage, {}, "ip", Category::Multiplayer};
    Setting<u16, true> multiplayer_port{linkage,    24872,  0,
                                        UINT16_MAX, "port", Category::Multiplayer};
    Setting<std::string> multiplayer_room_nickname{
        linkage, {}, "room_nickname", Category::Multiplayer};
    Setting<std::string> multiplayer_room_name{linkage, {}, "room_name", Category::Multiplayer};
    Setting<u8, true> multiplayer_max_player{linkage, 8, 0, 8, "max_player", Category::Multiplayer};
    Setting<u16, true> multiplayer_room_port{linkage,    24872,       0,
                                             UINT16_MAX, "room_port", Category::Multiplayer};
    Setting<u8, true> multiplayer_host_type{linkage, 0, 0, 1, "host_type", Category::Multiplayer};
    Setting<unsigned long long> multiplayer_game_id{linkage, {}, "game_id", Category::Multiplayer};
    Setting<std::string> multiplayer_room_description{
        linkage, {}, "room_description", Category::Multiplayer};
    std::pair<std::vector<std::string>, std::vector<std::string>> multiplayer_ban_list;

    // Game List
    Setting<bool> show_add_ons{linkage, true, "show_add_ons", Category::UiGameList};
    Setting<u32, true> game_icon_size{linkage, 64, 0, 512, "game_icon_size", Category::UiGameList};
    Setting<u32, true> folder_icon_size{linkage, 48, 8, 512, "folder_icon_size", Category::UiGameList};
    Setting<u8> row_1_text_id{linkage, 3, "row_1_text_id", Category::UiGameList};
    Setting<u8> row_2_text_id{linkage, 2, "row_2_text_id", Category::UiGameList};
    Setting<Settings::GameListMode> game_list_mode{linkage, Settings::GameListMode::CarouselView, "game_list_mode", Category::UiGameList};
    Setting<bool> show_game_name{linkage, true, "show_game_name", Category::UiGameList};
    /// The picked game's details beside the home view and the grid.
    Setting<bool> show_game_details{linkage, true, "show_game_details", Category::UiGameList};
    /// The status bar shows build 11's graphics buttons and the FSR sizes (its DETAILS toggle).
    Setting<bool> show_status_details{linkage, false, "show_status_details", Category::Ui};
    /// Customize menu (phoenix/util/phoenix_theme.h): the PhoenixEmu theme's accent and background
    /// colours (#rrggbb; empty is the theme's own) and button shape (0 pill, 1 soft, 2 square).
    Setting<std::string> theme_accent{linkage, std::string{}, "theme_accent", Category::Ui};
    Setting<std::string> theme_background{linkage, std::string{}, "theme_background", Category::Ui};
    Setting<u32> theme_shape{linkage, 0, "theme_shape", Category::Ui};
    /// The home view's tile size, in percent of the size that suits the window (its slider).
    Setting<u32, true> home_tile_scale{linkage, 100, 60, 160, "home_tile_scale", Category::UiGameList};
    /// How game cards look: 0 plain tiles, 1 a Switch or Switch 2 badge frame, 2 a cartridge,
    /// 3 a game case (PyroCade-like).
    Setting<u32, true> game_card_style{linkage, 1, 0, 3, "game_card_style", Category::UiGameList};
    /// The home view's carousel: 0 a stack (one big card, the others tilted behind it), 1 a row.
    Setting<u32, true> carousel_layout{linkage, 0, 0, 2, "carousel_layout", Category::UiGameList};
    /// The centred card's share of the height: 0 full, 1 3/4, 2 2/3, 3 1/2, 4 1/3.
    Setting<u32, true> carousel_card_size{linkage, 2, 0, 4, "carousel_card_size", Category::UiGameList};
    Setting<u32, true> carousel_side_count{linkage, 5, 3, 8, "carousel_side_count", Category::UiGameList};
    /// The picked game's picture as the home background, sharp rather than a soft glow.
    Setting<bool> carousel_banner_background{linkage, true, "carousel_banner_background",
                                             Category::UiGameList};
    /// The home screen's background: 0 the animated scene, 1 a picture, 2 one colour, 3 a gradient.
    Setting<u32, true> home_bg_kind{linkage, 0, 0, 3, "home_bg_kind", Category::UiGameList};
    Setting<std::string> home_bg_picture{linkage, {}, "home_bg_picture", Category::UiGameList};
    /// Pictures used before, newest first, joined by '|'.
    Setting<std::string> home_bg_recent{linkage, {}, "home_bg_recent", Category::UiGameList};
    Setting<std::string> home_bg_color{linkage, "#2e0c08", "home_bg_color", Category::UiGameList};
    Setting<std::string> home_bg_color2{linkage, "#0f0806", "home_bg_color2", Category::UiGameList};
    Setting<u32, true> home_bg_angle{linkage, 135, 0, 359, "home_bg_angle", Category::UiGameList};
    /// The moving parts over any background.
    Setting<bool> home_anim_enabled{linkage, true, "home_anim_enabled", Category::UiGameList};
    Setting<bool> home_anim_rays{linkage, true, "home_anim_rays", Category::UiGameList};
    /// 0 none, 1 dust, 2 embers, 3 both.
    Setting<u32, true> home_anim_particles{linkage, 3, 0, 3, "home_anim_particles", Category::UiGameList};
    /// 0 natural (dust wanders, embers rise), 1 everything rises, 2 everything falls, 3 swirl.
    Setting<u32, true> home_anim_motion{linkage, 0, 0, 3, "home_anim_motion", Category::UiGameList};
    Setting<u32, true> home_anim_amount{linkage, 100, 25, 200, "home_anim_amount", Category::UiGameList};
    Setting<u32, true> home_anim_speed{linkage, 100, 25, 300, "home_anim_speed", Category::UiGameList};
    Setting<bool> home_anim_game_glow{linkage, true, "home_anim_game_glow", Category::UiGameList};
    Setting<bool> home_anim_pulse{linkage, true, "home_anim_pulse", Category::UiGameList};
    Setting<bool> home_anim_vignette{linkage, true, "home_anim_vignette", Category::UiGameList};

    std::atomic_bool is_game_list_reload_pending{false};
    Setting<bool> cache_game_list{linkage, true, "cache_game_list", Category::UiGameList};
    Setting<bool> favorites_expanded{linkage, true, "favorites_expanded", Category::UiGameList};
    QVector<u64> favorited_ids;
    QMap<u64, QDir> ryujinx_link_paths;

    // perf overlay
    Setting<bool> show_perf_overlay{linkage, false, "show_perf_overlay", Category::UiGameList};
    // Where the performance bar sits (0-5: top left, top centre, top right, bottom left, bottom
    // centre, bottom right; 6: where it was dragged, as thousandths of the free room) and which
    // of its parts show.
    Setting<int> perf_overlay_anchor{linkage, 0, "perf_overlay_anchor", Category::UiGameList};
    Setting<int> perf_overlay_x{linkage, 0, "perf_overlay_x", Category::UiGameList};
    Setting<int> perf_overlay_y{linkage, 0, "perf_overlay_y", Category::UiGameList};
    Setting<bool> perf_overlay_compact{linkage, false, "perf_overlay_compact",
                                       Category::UiGameList};
    Setting<bool> perf_overlay_graph{linkage, true, "perf_overlay_graph", Category::UiGameList};
    Setting<bool> perf_overlay_memory{linkage, true, "perf_overlay_memory", Category::UiGameList};
    Setting<int> perf_overlay_opacity{linkage, 85, "perf_overlay_opacity", Category::UiGameList};

    // Size & File Types Column
    Setting<bool> show_size{linkage, true, "show_size", Category::UiGameList};
    Setting<bool> show_types{linkage, true, "show_types", Category::UiGameList};

    // Play time
    Setting<bool> show_play_time{linkage, true, "show_play_time", Category::UiGameList};

    // misc
    Setting<bool> show_fw_warning{linkage, true, "show_fw_warning", Category::Miscellaneous};

    bool configuration_applied;
    bool reset_to_defaults;
    bool shortcut_already_warned{false};
};

extern Values values;

u32 CalculateWidth(u32 height, Settings::AspectRatio ratio);

void SaveWindowState();
void RestoreWindowState(std::unique_ptr<QtConfig>& qtConfig);

// sync with uisettings.cpp
extern const std::array<Shortcut, 34> default_hotkeys;

} // namespace UISettings

Q_DECLARE_METATYPE(UISettings::GameDir*);

// These metatype declarations cannot be in common/settings.h because core is devoid of QT
Q_DECLARE_METATYPE(Settings::CpuAccuracy);
Q_DECLARE_METATYPE(Settings::GpuAccuracy);
Q_DECLARE_METATYPE(Settings::DmaAccuracy);
Q_DECLARE_METATYPE(Settings::FullscreenMode);
Q_DECLARE_METATYPE(Settings::NvdecEmulation);
Q_DECLARE_METATYPE(Settings::ResolutionSetup);
Q_DECLARE_METATYPE(Settings::ScalingFilter);
Q_DECLARE_METATYPE(Settings::AntiAliasing);
Q_DECLARE_METATYPE(Settings::RendererBackend);
Q_DECLARE_METATYPE(Settings::AstcRecompression);
Q_DECLARE_METATYPE(Settings::AstcDecodeMode);
Q_DECLARE_METATYPE(Settings::SpirvOptimizeMode);
