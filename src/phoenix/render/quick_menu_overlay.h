// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <deque>
#include <filesystem>
#include <QString>
#include <QWidget>

class MainWindow;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPainter;
class QPushButton;
class QSlider;
namespace Core {
struct PerfStatsResults;
}
namespace VideoCore {
class ShaderNotify;
}

/// The in-game menu (Guide button, F10, Shift+F2): a panel at the game's top right with the
/// game, a row of actions with their keys (pause, restart, stop, capture, album, full screen),
/// what is on now (frame generation, turbo, the performance HUD, mods), a preset, resolution,
/// frame generation, console mode, volume, the controller and amiibo, save slots, and a way to all
/// of the game's settings. While it is open a performance card sits at the game's top left and a
/// status strip along its bottom.
class QuickMenuOverlay : public QWidget {
    Q_OBJECT

public:
    explicit QuickMenuOverlay(MainWindow* parent);
    ~QuickMenuOverlay() override;

    void Toggle();
    void RefreshValues();

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void Reposition();
    void UpdateSlotInfo(int slot_index);
    void SaveSelectedSlot();
    void LoadSelectedSlot();
    void ApplyPreset(int preset);
    void ShowNote(const QString& text);
    std::filesystem::path GetSlotDir(int slot_index) const;
    /// The key set for a hotkey (Configure > Hotkeys), as the player sees it.
    [[nodiscard]] QString KeyFor(const char* action) const;
    void UpdateStats(const Core::PerfStatsResults& results);
    void PlacePanels();
    void PaintPerfCard(QPainter& p, const QRect& r) const;
    void PaintStatusStrip(QPainter& p, const QRect& r) const;
    [[nodiscard]] QString RendererText() const;

    MainWindow* m_main_window = nullptr;

    QLabel* m_game_icon = nullptr;
    QLabel* m_game_title = nullptr;
    QLabel* m_game_played = nullptr;
    QPushButton* m_pause_button = nullptr;
    QPushButton* m_frame_gen_chip = nullptr;
    QPushButton* m_turbo_chip = nullptr;
    QPushButton* m_hud_chip = nullptr;
    QPushButton* m_mods_chip = nullptr;
    QLabel* m_preset_hint = nullptr;
    QLabel* m_controller_label = nullptr;
    QButtonGroup* m_preset_group = nullptr;
    QComboBox* m_resolution_box = nullptr;
    QComboBox* m_frame_gen_box = nullptr;
    QButtonGroup* m_mode_group = nullptr;
    QSlider* m_volume_slider = nullptr;
    QLabel* m_volume_label = nullptr;
    QLabel* m_note = nullptr;
    bool m_refreshing = false;

    /// The performance card (top left) and status strip (bottom) shown with the menu.
    QWidget* m_perf_card = nullptr;
    QWidget* m_status_strip = nullptr;
    struct Stats {
        bool valid = false;
        double fps = 0.0;  ///< What reaches the screen (with frame generation)
        double base = 0.0; ///< What the game renders
        double low = 0.0;
        double frame_ms = 0.0;
        double speed = 0.0;
        std::deque<double> history; ///< Base frame rates, for the graph
        QString upscale;
        QString frame_gen;
        QString memory;
    };
    Stats m_stats;

    int m_selected_slot = 1;
    std::array<QPushButton*, 5> m_slot_buttons{};
    QLabel* m_slot_thumbnail = nullptr;
    QLabel* m_slot_meta_label = nullptr;
    QPushButton* m_slot_save_btn = nullptr;
    QPushButton* m_slot_load_btn = nullptr;
    QPushButton* m_slot_restart_btn = nullptr;
};
