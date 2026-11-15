// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class MainWindow;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

/// An in-game floating glassmorphic quick-settings panel summoned via Guide button, F10, or Shift+F2.
/// Allows live adjustment of volume, docked/handheld mode, FSR sharpness, and frame generation
/// without leaving or minimizing the game.
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

    MainWindow* m_main_window = nullptr;

    QLabel* m_game_title = nullptr;
    QSlider* m_volume_slider = nullptr;
    QPushButton* m_mute_button = nullptr;
    QPushButton* m_docked_button = nullptr;
    QCheckBox* m_frame_gen_check = nullptr;
    QSlider* m_sharpness_slider = nullptr;
    QLabel* m_sharpness_label = nullptr;
    QCheckBox* m_perf_overlay_check = nullptr;
    QPushButton* m_screenshot_button = nullptr;
    QPushButton* m_resume_button = nullptr;
};
