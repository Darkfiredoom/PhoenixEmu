// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QFrame>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QTimer>

#include "common/common_types.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace Core {
class System;
}
namespace PlayTime {
class PlayTimeManager;
}

/// The picked game beside the home view and the grid: its banner, name and developer, play time
/// and size, its updates and DLC (switchable here), its shader caches with a pre-compile
/// button, technical details, and Play / Properties / Favorite / more. After the game details
/// sidebar of Eglantine's home screen redesign.
class GameDetailsPanel : public QFrame {
    Q_OBJECT

public:
    struct Actions {
        std::function<void(const QString& path, u64 program_id)> play;
        std::function<void(const QString& path)> properties;
        std::function<void(u64 program_id)> toggle_favorite;
        std::function<bool(u64 program_id)> is_favorite;
        std::function<void(u64 program_id, const QString& path)> precompile_shaders;
        std::function<void(const QModelIndex& game, const QPoint& global_pos)> more;
        /// The add-ons changed: save the config.
        std::function<void()> addons_changed;
    };

    GameDetailsPanel(Core::System& system, const PlayTime::PlayTimeManager& play_time,
                     Actions actions, QWidget* parent = nullptr);

    /// The game to show (an invalid index shows nothing). Cheap: the files are read a moment
    /// later, once the selection settles.
    void SetGame(const QModelIndex& game);
    /// Reads the game's caches and add-ons again (after a pre-compile, for example).
    void Refresh();

signals:
    void CloseRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Addon {
        QString name;
        QString version;
        std::string key; ///< Its entry in Settings::values.disabled_addons
        bool update;
        bool enabled;
    };

    void Load();
    void PaintBanner();
    void ShowPictures();
    void ApplyAddons();
    void UpdateFavorite();

    Core::System& system;
    const PlayTime::PlayTimeManager& play_time;
    Actions actions;
    QPersistentModelIndex game;
    QTimer settle;

    QLabel* banner{};
    QLabel* icon{};
    QLabel* title{};
    QLabel* developer{};
    QLabel* played{};
    QLabel* size{};
    QWidget* addons_box{};
    QVBoxLayout* addons_list{};
    std::vector<std::pair<QCheckBox*, Addon>> addons;
    QLabel* no_addons{};
    QLabel* pipeline_cache{};
    QLabel* precompiled{};
    QPushButton* precompile{};
    QToolButton* details_toggle{};
    QWidget* details{};
    QLabel* details_text{};
    QPushButton* play_button{};
    QToolButton* favorite{};
    QPixmap banner_source;
    const QAbstractItemModel* watched_model{};
    QMetaObject::Connection picture_watch;
};
