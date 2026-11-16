// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <QWidget>

class MainWindow;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

/// An in-game floating glassmorphic quick-settings panel summoned via Guide button, F10, or Shift+F2.
/// Allows live adjustment of volume, docked/handheld mode, FSR sharpness, frame generation,
/// and visual save state slots without leaving or minimizing the game.
class QuickMenuOverlay : public QWidget {
    Q_OBJECT

public:
    explicit QuickMenuOverlay(MainWindow* parent);
    ~QuickMenuOverlay() override;

    void Toggle();
    void RefreshValues();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void Reposition();
    void UpdateSlotInfo(int slot_index);
    void SaveSelectedSlot();
    void LoadSelectedSlot();
    std::filesystem::path GetSlotDir(int slot_index) const;

    MainWindow* m_main_window = nullptr;

    QLabel* m_game_title = nullptr;
    QSlider* m_volume_slider = nullptr;
    QPushButton* m_mute_button = nullptr;
    QPushButton* m_docked_button = nullptr;
    QCheckBox* m_frame_gen_check = nullptr;
    QCheckBox* m_anr_check = nullptr;
    QSlider* m_sharpness_slider = nullptr;
    QLabel* m_sharpness_label = nullptr;
    QCheckBox* m_perf_overlay_check = nullptr;
    QPushButton* m_screenshot_button = nullptr;
    QPushButton* m_resume_button = nullptr;

    int m_selected_slot = 1;
    std::array<QPushButton*, 5> m_slot_buttons{};
    QLabel* m_slot_thumbnail = nullptr;
    QLabel* m_slot_meta_label = nullptr;
    QPushButton* m_slot_save_btn = nullptr;
    QPushButton* m_slot_load_btn = nullptr;
    QPushButton* m_slot_restart_btn = nullptr;
};
