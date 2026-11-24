// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include "common/common_types.h"

class QWidget;

enum class IconSearchStatus {
    Found,
    Choices,
    NotFound,
    Timeout,
    NetworkError,
    Cancelled,
    NeedsApiKey,
    BadApiKey,
    InternalError,
};

struct IconSearchResult {
    IconSearchStatus status{IconSearchStatus::NotFound};
    std::vector<u8> icon_bytes;
    std::vector<std::vector<u8>> choices;
    std::string resolved_title;
    std::string error;
};

// Looks an icon up by game name on switchiconshowdown.com, falling back to SteamGridDB when the
// user has set an API key. The search runs in the background under one 10 second deadline; errors,
// the API key prompt and the choice between several icons are handled here, so a caller only
// connects IconSelected.
struct GamePictureLookup {
    std::vector<u8> png;        ///< 256x256 PNG, empty when none was found.
    bool nothing_found = false; ///< The sources answered and had nothing (not a network error).
};

/// One game in PhoenixEmu's picture list: its picture, and its banner (a wide "hero") if any.
struct GamePictureListEntry {
    std::string url;
    std::string hero;
};

/// PhoenixEmu's title ID -> picture link list (dist/game_pictures.json in the update repository),
/// downloaded at most once a day. Empty when it can't be had. Blocks; run it off the UI thread.
std::unordered_map<u64, GamePictureListEntry> LoadGamePictureList(const std::atomic_bool& cancel);

/// Downloads a picture from the list as a 256x256 PNG. Empty on failure. Blocks.
std::vector<u8> DownloadGamePicture(const std::string& url, const std::atomic_bool& cancel);

/// What fetching a game's case art from PhoenixEmu's pack did.
enum class CaseArtFetch { Downloaded, AlreadyHere, NotInPack, Failed };

/// Fetches a game's case art (cover, inside, spine, back, banner) from PhoenixEmu's case art
/// pack (the case-art branch of the update repository: manifest.json, then the game's files)
/// into CaseArt::PackFolder(). The game is found by title ID, else by name. Blocks; run it off
/// the UI thread. The manifest is downloaded at most once a day.
CaseArtFetch DownloadCaseArt(u64 title_id, const std::string& name, bool switch2,
                             const std::atomic_bool& cancel);

/// Fetches a Switch 1 game's case scans (front, back, spine, inside) from Switch Cabinet
/// (cdn.switch-images-julio.com) into CaseArt::PackFolder()/Cases/<title ID>. The game is found
/// by title ID, else by name. Blocks; the case list is downloaded at most once a day.
CaseArtFetch DownloadSwitchCabinetArt(u64 title_id, const std::string& name,
                                      const std::atomic_bool& cancel);

/// Downloads a banner from the list as a JPEG of at most 1920 px across. Empty on failure.
std::vector<u8> DownloadGameBanner(const std::string& url, const std::atomic_bool& cancel);

/// Finds a game picture without asking anything: SteamGridDB's first icon when api_key is set,
/// else switchiconshowdown.com. Blocks; run it off the UI thread.
GamePictureLookup FindGamePictureAutomatically(const std::string& name, u64 title_id,
                                               const std::string& api_key,
                                               const std::atomic_bool& cancel);

class IconSearch : public QObject {
    Q_OBJECT
public:
    explicit IconSearch(QWidget* parent);
    ~IconSearch() override;

    void Start(const QString& name, u64 title_id = 0);
    bool IsRunning() const;

signals:
    void RunningChanged(bool running);
    // A 256x256 PNG, and the canonical title when the name was an alias (else empty).
    void IconSelected(const QByteArray& png, const QString& resolved_title);

private:
    void OnFinished();
    int ChooseIcon(const std::vector<std::vector<u8>>& choices);
    bool AskForApiKey(bool missing);

    QWidget* parent_widget;
    QFutureWatcher<IconSearchResult> watcher;
    std::shared_ptr<std::atomic_bool> cancel_flag{std::make_shared<std::atomic_bool>(false)};
    QString last_name;
    u64 last_title_id{};
};
