// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <QImage>
#include <QString>

#include "common/common_types.h"

/// The player's own game case scans, for the Case look and the case that opens (after PyroCade).
/// A folder (Customize > Games > Case art folder) holds:
///   Cases/<game>/<game>.jpg          the whole cover: back | spine | front, side by side
///   Cases/<game>/<game>-Inside.jpg   the inside of the cover, seen through the open case
///   or Cases/<game>/<game>-Front.jpg, -Back.jpg, -Side.jpg (the spine, lying down)
///   Banner/<game>.jpg                a wide picture for the home background
/// <game> is matched to the game's name loosely ("Pokopia" finds "Pokémon Pokopia", "OOT" the
/// initials of "Ocarina of Time"); a folder named after the title ID always matches.
namespace CaseArt {

struct Art {
    QImage front;  ///< The cover's front, upright
    QImage back;   ///< The back, upright (may be null)
    QImage spine;  ///< The spine, upright (text running top to bottom; may be null)
    QImage inside; ///< The inside of the cover, both halves side by side (may be null)
};

/// The folder in use: the setting, else <user folder>/case_art.
[[nodiscard]] std::filesystem::path Folder();

/// The game's case art, or null when the folder has none for it. Reads the files the first time
/// (any thread); cached after.
[[nodiscard]] std::shared_ptr<const Art> Load(const QString& title, u64 program_id);

/// The game's banner file (JPEG or PNG bytes), or empty.
[[nodiscard]] std::vector<u8> BannerBytes(const QString& title, u64 program_id);

/// The name to show for a game: when its name came from a file ("Pokopia_v2.0.1a (update)")
/// and holds the name of a case art folder ("Pokopia"), that folder's name; else `title`.
[[nodiscard]] QString DisplayName(const QString& title, u64 program_id);

/// Where the downloaded case art pack is kept (PhoenixEmu's case-art branch, a game at a time).
[[nodiscard]] std::filesystem::path PackFolder();
/// How well a name fits the game (0 none; 600 initials; 800+ found in the title; 1000 the ID).
[[nodiscard]] int MatchScore(const QString& candidate, const QString& title, u64 program_id);
/// The pack had nothing for the game in the last day (so it isn't asked again yet).
[[nodiscard]] bool PackRecentlyMissing(u64 program_id);
void MarkPackMissing(u64 program_id);

/// Forgets what was read (the folder or its files changed).
void Reset();

} // namespace CaseArt
