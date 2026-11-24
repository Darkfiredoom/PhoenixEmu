// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QButtonGroup;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QVBoxLayout;
class QWidget;

/// The look the settings windows share (Settings and a game's settings): a sidebar of pages
/// with drawn icons and a search box, a header naming the page, and pill-shaped sub-tabs.
namespace SettingsShell {

enum class Icon {
    General,
    Library,
    Graphics,
    Audio,
    Controls,
    System,
    Multiplayer,
    Shortcuts,
    Cpu,
    Debug,
    Applets,
    Overview,
    FrameRate,
    Mods,
    Artwork,
    Network,
};

/// A line icon for a sidebar row, in its own colour.
[[nodiscard]] QIcon MakeIcon(Icon icon);

struct Sidebar {
    QFrame* frame{};
    QVBoxLayout* top{};  ///< Above the title: a game's picture and name, for a game's settings
    QLabel* title{};
    QLineEdit* search{};
    QListWidget* list{};
    QLabel* footer{};
};

/// Puts `list` (the window's page list) in a sidebar under `title` and a search box.
[[nodiscard]] Sidebar BuildSidebar(QWidget* parent, QListWidget* list, const QString& title,
                                   const QString& search_hint);

struct Header {
    QWidget* widget{};
    QLabel* title{};
    QLabel* subtitle{};
    QHBoxLayout* right{}; ///< Buttons at the header's right
};

[[nodiscard]] Header BuildHeader(QWidget* parent);

/// An "Everyday | Advanced" switch; `advanced` is the one checked first. Ids: 0 and 1.
[[nodiscard]] QButtonGroup* BuildModeSwitch(QWidget* parent, QHBoxLayout* into, bool advanced);

/// True when some text a player reads in `root` (labels, buttons, check boxes, group titles,
/// list and combo box entries, tooltips aside) contains `query`, case aside.
[[nodiscard]] bool ContainsText(const QWidget* root, const QString& query);

} // namespace SettingsShell
