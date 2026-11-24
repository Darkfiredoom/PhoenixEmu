// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <vector>
#include <QDateTime>
#include <QDialog>
#include <QPixmap>
#include <QSize>

#include "common/common_types.h"

class GameList;
class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/// The screenshots taken in games (Capture, Ctrl+P): grouped by day in a grid, filtered by game
/// with pills, the picked one large beside them with its details, and copy, show in folder, use
/// as the game's banner on the home screen, and delete.
class AlbumDialog : public QDialog {
    Q_OBJECT

public:
    explicit AlbumDialog(QWidget* parent = nullptr, GameList* game_list = nullptr);
    ~AlbumDialog() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Capture {
        std::filesystem::path path;
        QString filename;
        QDateTime taken;
        u64 program_id = 0;
        QSize resolution;
        u64 file_size = 0;
        QPixmap thumbnail;
    };
    struct Game {
        QString name;
        QPixmap picture;
    };

    void Reload();
    void RebuildPills();
    void RebuildGrid();
    void Select(int index);
    [[nodiscard]] std::vector<int> Shown() const;
    [[nodiscard]] Game GameOf(u64 program_id);
    void CopyToClipboard();
    void ShowInFolder();
    void UseAsBanner();
    void DeleteCurrent();
    void Note(const QString& text);

    GameList* m_game_list = nullptr;
    std::vector<Capture> m_captures;
    std::vector<std::pair<u64, Game>> m_games; ///< The games the captures are from, cached
    int m_current = -1;
    u64 m_filter = 0; ///< 0: all games
    bool m_newest_first = true;
    int m_columns = 0;

    QLabel* m_count_label = nullptr;
    QHBoxLayout* m_pill_row = nullptr;
    QButtonGroup* m_pill_group = nullptr;
    QComboBox* m_order = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_grid = nullptr;
    QVBoxLayout* m_grid_layout = nullptr;
    std::vector<QWidget*> m_thumbs; ///< Index into m_captures by the thumb's "capture" property

    QLabel* m_preview = nullptr;
    QLabel* m_game_icon = nullptr;
    QLabel* m_game_name = nullptr;
    QLabel* m_meta = nullptr;
    QLabel* m_fact_game = nullptr;
    QLabel* m_fact_file = nullptr;
    QLabel* m_fact_format = nullptr;
    QLabel* m_note = nullptr;
    QPushButton* m_copy_btn = nullptr;
    QPushButton* m_folder_btn = nullptr;
    QPushButton* m_banner_btn = nullptr;
    QPushButton* m_delete_btn = nullptr;
};
