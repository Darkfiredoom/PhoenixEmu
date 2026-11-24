// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <vector>

#include <QListView>
#include <QPixmap>
#include <QString>

#include "phoenix/util/phoenix_backdrop.h"

class GameCard;
class GameListModel;
class QResizeEvent;

class GameCarousel : public QListView {
    Q_OBJECT

public:
    explicit GameCarousel(QWidget* parent = nullptr);
    ~GameCarousel() override;

    void SetModel(GameListModel* model);
    /// Picks the first game still shown when the picked one is hidden (a library tab).
    void EnsureVisiblePick();
    void ApplyFilter(const QString& edit_filter_text, GameListModel* model);
    void UpdateIconSize();
    /// The name on the home view's user button.
    void SetUserName(const QString& name);
    /// How many gamepads are connected (the chip by the user button).
    void SetControllerCount(int count);

    QModelIndex indexAt(const QPoint& point) const override;
    QRect visualRect(const QModelIndex& index) const override;

signals:
    /// The home view's "+ Add games" button.
    void AddDirectoryRequested();
    /// The home view's user button: the user menu opens at this global position.
    void UserMenuRequested(const QPoint& global_pos);
    void CustomizeRequested();
    /// The picked game's case was clicked (the Case look): open it.
    void CaseOpenRequested(const QModelIndex& game);
    /// The Cartridge look: the picked cartridge held up to be turned over (CartridgeViewer).
    void CartridgeOpenRequested(const QModelIndex& game);
    void ControllersRequested();
    /// The Library layout's hero: "Game settings" and the "..." button (the game's menu).
    void GameSettingsRequested(const QModelIndex& game);
    void GameMenuRequested(const QModelIndex& game, const QPoint& global_pos);
    /// The Library layout's list button: the game list as a list.
    void ListViewRequested();

protected:
    /// The warm background, a greeting and a clock, then the tiles.
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    // The stack lays out, paints and navigates its cards itself; the row leaves it to QListView.
    void scrollTo(const QModelIndex& index, ScrollHint hint) override;
    QModelIndex moveCursor(CursorAction cursor_action, Qt::KeyboardModifiers modifiers) override;
    void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override;
    QRegion visualRegionForSelection(const QItemSelection& selection) const override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    /// UISettings::carousel_layout 3: the Library (PhoenixEmu's Paper design): the picked game
    /// large at the top (its badge, title, Play, Game settings and its menu), the games in a grid
    /// under it: covers, cases on shelves or cartridges in a rack, by the game card look.
    [[nodiscard]] static bool LibraryLayout();
    /// The views that lay out and paint their cards themselves (the stack, shelf and library).
    [[nodiscard]] static bool CustomLayout();
    struct LibraryGeometry {
        int left = 48;
        int hero_badge_y = 0;  ///< The badge line's middle
        int hero_title_y = 0;  ///< The title's baseline
        int heading_y = 0;     ///< The "Library" heading's top
        QRect tiles;           ///< Where the tiles are (they scroll inside it)
        int columns = 1;
        QSizeF tile;           ///< A tile's picture (a cover, a case, a cartridge slot)
        QSizeF cell;           ///< A tile with its gaps and name
        QRectF hero_art;       ///< The case or cartridge at the top right (Case, Cartridge looks)
    };
    [[nodiscard]] LibraryGeometry Library() const;
    [[nodiscard]] QRectF LibraryTileRect(int position, const LibraryGeometry& g) const;
    [[nodiscard]] QModelIndex LibraryIndexAt(const QPoint& point) const;
    void PaintLibrary(QPainter& painter);
    void ScrollLibraryTo(int position);
    qreal m_libraryScroll = 0.0;
    class QPushButton* m_settingsButton = nullptr;
    class QPushButton* m_moreButton = nullptr;
    /// Grid, list, cases, cartridges: the Library heading's view buttons.
    std::vector<class QPushButton*> m_viewButtons;
    /// UISettings::carousel_layout: one big card with the others tilted behind it (after
    /// Eglantine's home screen redesign), else the row of tiles.
    [[nodiscard]] static bool Stacked();
    /// The rows the filter leaves, left to right, and a row's place among them.
    [[nodiscard]] std::vector<int> VisibleRows() const;
    int PositionOf(int row) const;
    [[nodiscard]] QModelIndex StackIndexAt(const QPoint& point) const;
    void MoveTo(int position);
    void PaintStack(QPainter& painter);
    /// Where the stack's cards go: between the greeting and the play button.
    [[nodiscard]] QRect StackArea() const;
    qreal m_position = 0.0;
    bool m_fullRepaintAsked = false; ///< A partial repaint was turned into a whole one
    class QVariantAnimation* m_positionAnimation = nullptr;
    bool m_ambientSoft = false;

    void PlaceAddButton();
    /// Keeps the user and Add games buttons' text readable on the backdrop behind them.
    void UpdateButtonContrast();

    GameCard* m_gameCard = nullptr;
    // The backdrop (util/phoenix_backdrop.h): the picked game's blurred picture, cross-faded, and
    // the moving scene.
    void RebuildAmbient();
    QPixmap m_ambientSource;
    bool m_ambientIsBanner = false; ///< m_ambientSource is the game's banner
    /// The picked game's background picture: its banner, else its picture.
    void SetAmbientSource(const QModelIndex& index);
    QPixmap m_ambient;
    QPixmap m_ambientPrevious;
    qreal m_ambientFade = 1.0;
    /// The quote under the greeting (Quotes() in carousel.cpp): a new one each time the home
    /// screen appears, and every ten minutes while it shows.
    int m_quote = 0;
    void PickQuote();
    class QVariantAnimation* m_ambientAnimation = nullptr;
    std::unique_ptr<PhoenixBackdrop::Scene> m_scene;
    PhoenixBackdrop::Scene::Options m_lastStillOptions;
    class QElapsedTimer* m_frameClock = nullptr;
    class QSlider* m_sizeSlider = nullptr;
    class QVariantAnimation* m_liftAnimation = nullptr;
    class QPushButton* m_addButton = nullptr;
    /// Left of "+ Add games": the current user, opens the user menu.
    class QPushButton* m_userButton = nullptr;
    class QPushButton* m_customizeButton = nullptr;
    class QPushButton* m_controllersButton = nullptr;
    int m_controllerCount = -1;
    /// Bottom left: Console hero play button for the picked game.
    class QPushButton* m_playButton = nullptr;
    /// Bottom right, left to right: the speaker (mutes every menu sound, menu_muted), the
    /// volume slider (menu_sound_volume), then the card size icon and slider.
    class QPushButton* m_musicButton = nullptr;
    class QSlider* m_volumeSlider = nullptr;
    class QLabel* m_sizeIcon = nullptr;
    void UpdateMusicButton();
    QString m_buttonTextColor;
    void PlaceHeroControls();
};
