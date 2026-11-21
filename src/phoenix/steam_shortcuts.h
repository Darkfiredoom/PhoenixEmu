// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <QImage>
#include <QString>

/// Games in the Steam library as non-Steam games, after Triforce Setup's Add to Steam: an entry in
/// each Steam user's shortcuts.vdf that starts PhoenixEmu with the game, with the game's icon as
/// its pictures. Steam writes that file itself when it closes, so nothing is changed while it
/// runs; the file as it was is kept beside it (shortcuts.vdf.phoenixemu-backup).
namespace SteamShortcuts {

struct Result {
    bool ok = false;
    QString message; ///< What happened, for the player
};

/// Steam's folder, or empty when Steam is not installed.
[[nodiscard]] std::filesystem::path FindSteam();
[[nodiscard]] bool SteamRunning();

/// Whether the game is in the library of a Steam user on this PC.
[[nodiscard]] bool IsAdded(const std::string& game_path);

/// Adds the game for every Steam user on this PC (once each).
/// @param exe        PhoenixEmu's executable (the AppImage on Linux)
/// @param launch     The arguments that start the game
Result Add(const std::string& game_path, const QString& title, const std::filesystem::path& exe,
           const std::string& launch, const QImage& icon);
/// Takes the game out of every Steam user's library, with its pictures.
Result Remove(const std::string& game_path);

} // namespace SteamShortcuts
