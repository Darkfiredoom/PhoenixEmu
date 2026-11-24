// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;

/// Settings > Graphics as most players want it (PhoenixEmu's Paper design): four presets, then
/// the picture (resolution, upscaling, anti-aliasing) and smoothness (frame generation, VSync,
/// docked mode). The full Graphics tabs stay under Advanced; this one saves only what was changed
/// on it, after them, so the two never undo each other.
class ConfigureGraphicsEveryday : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureGraphicsEveryday(QWidget* parent = nullptr);

    void SetConfiguration();
    void ApplyConfiguration();

private:
    void UpdatePresets();
    void UpdateResolutionHint();

    std::array<QAbstractButton*, 4> presets{};
    QButtonGroup* preset_group = nullptr;
    QComboBox* resolution = nullptr;
    QLabel* resolution_hint = nullptr;
    QComboBox* upscaling = nullptr;
    QComboBox* anti_aliasing = nullptr;
    QComboBox* frame_gen = nullptr;
    QAbstractButton* vsync = nullptr;
    QAbstractButton* docked = nullptr;

    bool touched_resolution = false;
    bool touched_upscaling = false;
    bool touched_anti_aliasing = false;
    bool touched_frame_gen = false;
    bool touched_vsync = false;
    bool touched_docked = false;
    bool loading = false;
};
