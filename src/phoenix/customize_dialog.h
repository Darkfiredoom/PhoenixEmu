// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <QColor>
#include <QDialog>
#include <QString>

#include "common/common_types.h"

class QAbstractButton;
class QComboBox;
class QGroupBox;
class QToolButton;

/// Customize > Appearance: the theme, accent and background colours and button shape in one
/// window. Every choice applies to the whole program at once, so the program is the preview;
/// Cancel puts back what was there when the window opened.
class CustomizeDialog : public QDialog {
    Q_OBJECT

public:
    struct Swatch {
        QString name;
        QColor color;
    };

    /// The colours the Customize menu and this window offer. The first of each is the default.
    static std::vector<Swatch> AccentPresets();
    static std::vector<Swatch> BackgroundPresets();

    /// `apply` restyles the program from UISettings (MainWindow::ApplyCustomization).
    explicit CustomizeDialog(std::function<void()> apply, QWidget* parent = nullptr);

    void reject() override;

private:
    struct ColourRow {
        std::string* setting{};
        QColor fallback;
        std::vector<std::pair<QToolButton*, QColor>> buttons;
        QToolButton* custom{};
    };

    QGroupBox* MakeColourRow(const QString& title, ColourRow& row, std::string& setting,
                             const std::vector<Swatch>& presets);
    void Apply();
    void Refresh();

    std::function<void()> apply;
    std::string theme, accent, background;
    u32 shape{};
    std::string old_theme, old_accent, old_background;
    u32 old_shape{};

    QComboBox* theme_box{};
    ColourRow accent_row, background_row;
    QGroupBox* accent_group{};
    QGroupBox* background_group{};
    QGroupBox* shape_group{};
    std::vector<QAbstractButton*> shape_buttons;
};
