// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <QColor>
#include <QFrame>
#include <QString>

#include "common/common_types.h"
#include "common/settings_enums.h"

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QToolButton;
class QVBoxLayout;
class QWidget;

/// Everything Customize offers, on one scrolling panel beside the game list: the theme, the home
/// screen's background and animation, the game cards and the home screen's sounds. Every choice
/// applies at once, so the program itself is the preview.
class CustomizePanel : public QFrame {
    Q_OBJECT

public:
    struct Swatch {
        QString name;
        QColor color;
    };

    /// The first of each is the default.
    static std::vector<Swatch> AccentPresets();
    static std::vector<Swatch> BackgroundPresets();

    /// `apply` repaints the program from UISettings, restyling it (the stylesheet, slow) only
    /// when `restyle` is set; `set_mode` switches the game list between tree, grid and home.
    /// `set_icon_size` changes the list's and grid's icon size (the game list reloads).
    CustomizePanel(std::function<void(bool restyle)> apply,
                   std::function<void(Settings::GameListMode)> set_mode,
                   std::function<void(u32)> set_icon_size, QWidget* parent = nullptr);

    /// Shows the settings as they are now (another part of the program may have changed them).
    void Refresh();

signals:
    void CloseRequested();
    /// The case art folder changed: the game list reads it again.
    void CaseArtFolderChanged();

private:
    struct ColourRow {
        std::function<std::string()> get;
        std::function<void(const std::string&)> set;
        QColor fallback;
        std::vector<std::pair<QToolButton*, QColor>> buttons;
        QToolButton* custom{};
    };

    QWidget* Section(const QString& title);
    QButtonGroup* Segments(QVBoxLayout* layout, const std::vector<QString>& names,
                           const std::function<void(int)>& picked);
    void ColourSwatches(QVBoxLayout* layout, ColourRow& row, const std::vector<Swatch>& presets,
                        const QString& title);
    QSlider* LabelledSlider(QVBoxLayout* layout, const QString& name, int min, int max,
                            const std::function<QString(int)>& text,
                            const std::function<void(int)>& changed, QLabel** value_label);
    QCheckBox* Check(QVBoxLayout* layout, const QString& name,
                     const std::function<void(bool)>& changed);
    QPushButton* ColourButton(const QString& title, const std::function<std::string()>& get,
                              const std::function<void(const std::string&)>& set);
    void ChoosePicture();
    void UsePicture(const QString& path);
    void ResetToDefaults();
    void Changed(bool restyle = false);

    std::function<void(bool)> apply;
    std::function<void(Settings::GameListMode)> set_mode;
    std::function<void(u32)> set_icon_size;
    bool refreshing = false;

    QComboBox* theme_box{};
    ColourRow accent_row, background_row;
    QWidget* theme_colours{};
    QButtonGroup* shape_group{};

    QButtonGroup* kind_group{};
    QWidget* picture_page{};
    QWidget* solid_page{};
    QWidget* gradient_page{};
    QLabel* picture_preview{};
    QWidget* recent_row{};
    QString shown_recent;
    QPushButton* solid_button{};
    QPushButton* gradient_start{};
    QPushButton* gradient_end{};
    QSlider* angle_slider{};
    QLabel* angle_label{};

    QCheckBox* animate_box{};
    QWidget* animation_options{};
    QCheckBox* rays_box{};
    QButtonGroup* particles_group{};
    QButtonGroup* motion_group{};
    QSlider* amount_slider{};
    QLabel* amount_label{};
    QSlider* speed_slider{};
    QLabel* speed_label{};
    QCheckBox* glow_box{};
    QCheckBox* pulse_box{};
    QCheckBox* vignette_box{};

    QButtonGroup* mode_group{};
    QButtonGroup* cards_group{};
    QWidget* carousel_options{};
    QButtonGroup* layout_group{};
    QWidget* stack_options{};
    QButtonGroup* card_size_group{};
    QButtonGroup* side_count_group{};
    QCheckBox* banner_box{};
    QCheckBox* details_box{};
    QLabel* case_art_label{};
    QWidget* icon_options{};
    QButtonGroup* icon_size_group{};

    QCheckBox* music_box{};
    QCheckBox* random_box{};
    QCheckBox* hum_box{};
    QCheckBox* clicks_box{};
    QCheckBox* mute_box{};

    std::vector<std::pair<QPushButton*, std::function<std::string()>>> colour_buttons;
};
