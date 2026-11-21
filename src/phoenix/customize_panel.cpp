// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include "phoenix/customize_panel.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/config/uisettings.h"

namespace {

constexpr int SwatchSize = 30;
constexpr int SwatchesPerRow = 6;
constexpr int RecentPictures = 4;
constexpr QSize PreviewSize{296, 120};

/// A round colour chip; `ring` draws the picked one's outline.
QIcon Chip(const QColor& color, bool ring) {
    QPixmap pixmap(SwatchSize * 2, SwatchSize * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    if (ring) {
        painter.setPen(QPen(QColor(255, 255, 255, 230), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(1.5, 1.5, SwatchSize - 3, SwatchSize - 3));
    }
    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.setBrush(color);
    painter.drawEllipse(QRectF(5, 5, SwatchSize - 10, SwatchSize - 10));
    return QIcon{pixmap};
}

/// The custom colour's chip: the colour when one is picked, a rainbow otherwise.
QIcon CustomChip(const QColor& color, bool picked) {
    if (picked) {
        return Chip(color, true);
    }
    QPixmap pixmap(SwatchSize * 2, SwatchSize * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    QConicalGradient wheel{QPointF(SwatchSize / 2.0, SwatchSize / 2.0), 0};
    for (int i = 0; i <= 6; ++i) {
        wheel.setColorAt(i / 6.0, QColor::fromHsvF(std::min(i / 6.0, 0.999), 0.85, 1.0));
    }
    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.setBrush(wheel);
    painter.drawEllipse(QRectF(5, 5, SwatchSize - 10, SwatchSize - 10));
    return QIcon{pixmap};
}

QColor Current(const std::string& setting, const QColor& fallback) {
    const QColor color{QString::fromStdString(setting)};
    return setting.empty() || !color.isValid() ? fallback : color;
}

QStringList RecentList() {
    return QString::fromStdString(UISettings::values.home_bg_recent.GetValue())
        .split(QLatin1Char('|'), Qt::SkipEmptyParts);
}

/// A small cover-cropped still of a picture, for the preview and the recent row.
QPixmap Thumbnail(const QString& path, QSize size) {
    QImageReader reader{path};
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid()) {
        reader.setScaledSize(full.scaled(size, Qt::KeepAspectRatioByExpanding));
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    return QPixmap::fromImage(image.copy((image.width() - size.width()) / 2,
                                         (image.height() - size.height()) / 2, size.width(),
                                         size.height()));
}

template <typename T>
void ResetSetting(T& setting) {
    setting.SetValue(setting.GetDefault());
}

struct ThemeChoice {
    const char* key;
    const char* name;
};
constexpr std::array<ThemeChoice, 4> Themes{{
    {"phoenix", QT_TRANSLATE_NOOP("CustomizePanel", "PhoenixEmu")},
    {"colorful_dark", QT_TRANSLATE_NOOP("CustomizePanel", "Classic (Dark Colorful)")},
    {"colorful_midnight_blue", QT_TRANSLATE_NOOP("CustomizePanel", "Classic Midnight Blue")},
    {"default", QT_TRANSLATE_NOOP("CustomizePanel", "System (follows your desktop)")},
}};

} // Anonymous namespace

std::vector<CustomizePanel::Swatch> CustomizePanel::AccentPresets() {
    return {
        {tr("Flame (default)"), PhoenixTheme::DefaultAccent},
        {tr("Crimson"), QColor(0xE6, 0x2E, 0x45)},
        {tr("Magma"), QColor(0xFF, 0x6A, 0x1F)},
        {tr("Tangerine"), QColor(0xFF, 0x8A, 0x3D)},
        {tr("Gold"), PhoenixTheme::BaseAccent},
        {tr("Lime"), QColor(0x9B, 0xE8, 0x3D)},
        {tr("Mint"), QColor(0x3D, 0xE8, 0xB0)},
        {tr("Ocean"), QColor(0x3D, 0xA5, 0xFF)},
        {tr("Grape"), QColor(0xA9, 0x7C, 0xFF)},
        {tr("Bubblegum"), QColor(0xFF, 0x6F, 0xCF)},
        {tr("Strawberry"), QColor(0xFF, 0x4F, 0x6D)},
        {tr("Snow"), QColor(0xE8, 0xE8, 0xE8)},
    };
}

std::vector<CustomizePanel::Swatch> CustomizePanel::BackgroundPresets() {
    return {
        {tr("Ember (default)"), PhoenixTheme::DefaultBackground},
        {tr("Obsidian"), QColor(0x12, 0x0C, 0x0C)},
        {tr("Ash"), QColor(0x20, 0x1C, 0x1B)},
        {tr("Warm Charcoal"), PhoenixTheme::BaseBackground},
        {tr("Cool Slate"), QColor(0x1B, 0x20, 0x26)},
        {tr("Deep Blue"), QColor(0x12, 0x1A, 0x2A)},
        {tr("Forest"), QColor(0x14, 0x20, 0x1A)},
        {tr("Plum"), QColor(0x22, 0x14, 0x2A)},
        {tr("Midnight Black"), QColor(0x16, 0x16, 0x16)},
    };
}

CustomizePanel::CustomizePanel(std::function<void(bool)> apply_,
                               std::function<void(Settings::GameListMode)> set_mode_,
                               std::function<void(u32)> set_icon_size_, QWidget* parent)
    : QFrame(parent), apply{std::move(apply_)}, set_mode{std::move(set_mode_)},
      set_icon_size{std::move(set_icon_size_)} {
    setObjectName(QStringLiteral("CustomizePanel"));
    setFixedWidth(340);
    setFrameShape(QFrame::NoFrame);

    auto* const outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* const header = new QHBoxLayout;
    header->setContentsMargins(16, 12, 10, 8);
    auto* const title = new QLabel(tr("Customize"));
    title->setObjectName(QStringLiteral("CustomizePanelTitle"));
    auto* const close = new QToolButton;
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Close"));
    close->setAutoRaise(true);
    connect(close, &QToolButton::clicked, this, &CustomizePanel::CloseRequested);
    header->addWidget(title, 1);
    header->addWidget(close);
    outer->addLayout(header);

    auto* const scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* const body = new QWidget;
    body->setObjectName(QStringLiteral("CustomizePanelBody"));
    auto* const layout = new QVBoxLayout(body);
    layout->setContentsMargins(16, 4, 16, 16);
    layout->setSpacing(6);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    const auto add_section = [&](const QString& name) {
        QWidget* const section = Section(name);
        layout->addWidget(section);
        return static_cast<QVBoxLayout*>(section->layout());
    };

    // --- Theme ---
    {
        QVBoxLayout* const s = add_section(tr("Theme"));
        theme_box = new QComboBox;
        for (const ThemeChoice& choice : Themes) {
            theme_box->addItem(tr(choice.name), QString::fromLatin1(choice.key));
        }
        connect(theme_box, &QComboBox::activated, this, [this](int index) {
            UISettings::values.theme = theme_box->itemData(index).toString().toStdString();
            Changed(true);
        });
        s->addWidget(theme_box);

        theme_colours = new QWidget;
        auto* const colours = new QVBoxLayout(theme_colours);
        colours->setContentsMargins(0, 0, 0, 0);
        colours->setSpacing(4);
        accent_row.get = [] { return UISettings::values.theme_accent.GetValue(); };
        accent_row.set = [](const std::string& v) { UISettings::values.theme_accent = v; };
        accent_row.fallback = PhoenixTheme::DefaultAccent;
        background_row.get = [] { return UISettings::values.theme_background.GetValue(); };
        background_row.set = [](const std::string& v) { UISettings::values.theme_background = v; };
        background_row.fallback = PhoenixTheme::DefaultBackground;
        colours->addWidget(new QLabel(tr("Accent colour")));
        ColourSwatches(colours, accent_row, AccentPresets(), tr("Accent colour"));
        colours->addWidget(new QLabel(tr("Window colour")));
        ColourSwatches(colours, background_row, BackgroundPresets(), tr("Window colour"));
        colours->addWidget(new QLabel(tr("Button shape")));
        shape_group = Segments(colours, {tr("Pills"), tr("Soft"), tr("Square")}, [this](int id) {
            UISettings::values.theme_shape = static_cast<u32>(id);
            Changed(true);
        });
        s->addWidget(theme_colours);
    }

    // --- Home background ---
    {
        QVBoxLayout* const s = add_section(tr("Home background"));
        kind_group = Segments(s, {tr("Animated"), tr("Picture"), tr("Solid"), tr("Gradient")},
                              [this](int id) {
                                  UISettings::values.home_bg_kind = static_cast<u32>(id);
                                  if (id == 1 &&
                                      UISettings::values.home_bg_picture.GetValue().empty()) {
                                      ChoosePicture();
                                  }
                                  Changed();
                              });

        picture_page = new QWidget;
        auto* const picture = new QVBoxLayout(picture_page);
        picture->setContentsMargins(0, 4, 0, 0);
        picture_preview = new QLabel;
        picture_preview->setFixedSize(PreviewSize);
        picture_preview->setAlignment(Qt::AlignCenter);
        picture_preview->setObjectName(QStringLiteral("CustomizePicturePreview"));
        picture->addWidget(picture_preview);
        auto* const choose = new QPushButton(tr("Choose Picture..."));
        connect(choose, &QPushButton::clicked, this, &CustomizePanel::ChoosePicture);
        picture->addWidget(choose);
        picture->addWidget(new QLabel(tr("Recent pictures")));
        recent_row = new QWidget;
        new QHBoxLayout(recent_row);
        recent_row->layout()->setContentsMargins(0, 0, 0, 0);
        recent_row->layout()->setSpacing(6);
        picture->addWidget(recent_row);
        s->addWidget(picture_page);

        solid_page = new QWidget;
        auto* const solid = new QHBoxLayout(solid_page);
        solid->setContentsMargins(0, 4, 0, 0);
        solid->addWidget(new QLabel(tr("Colour")));
        solid_button = ColourButton(
            tr("Background colour"), [] { return UISettings::values.home_bg_color.GetValue(); },
            [](const std::string& v) { UISettings::values.home_bg_color = v; });
        solid->addWidget(solid_button, 1);
        s->addWidget(solid_page);

        gradient_page = new QWidget;
        auto* const gradient = new QVBoxLayout(gradient_page);
        gradient->setContentsMargins(0, 4, 0, 0);
        auto* const ends = new QHBoxLayout;
        gradient_start = ColourButton(
            tr("Gradient start"), [] { return UISettings::values.home_bg_color.GetValue(); },
            [](const std::string& v) { UISettings::values.home_bg_color = v; });
        gradient_end = ColourButton(
            tr("Gradient end"), [] { return UISettings::values.home_bg_color2.GetValue(); },
            [](const std::string& v) { UISettings::values.home_bg_color2 = v; });
        auto* const swap = new QToolButton;
        swap->setText(QStringLiteral("⇄"));
        swap->setToolTip(tr("Swap the colours"));
        connect(swap, &QToolButton::clicked, this, [this] {
            const std::string first = UISettings::values.home_bg_color.GetValue();
            UISettings::values.home_bg_color = UISettings::values.home_bg_color2.GetValue();
            UISettings::values.home_bg_color2 = first;
            Changed();
        });
        ends->addWidget(gradient_start, 1);
        ends->addWidget(swap);
        ends->addWidget(gradient_end, 1);
        gradient->addLayout(ends);
        angle_slider = LabelledSlider(
            gradient, tr("Angle"), 0, 359, [](int v) { return QStringLiteral("%1°").arg(v); },
            [](int v) { UISettings::values.home_bg_angle = static_cast<u32>(v); }, &angle_label);
        s->addWidget(gradient_page);
    }

    // --- Animation ---
    {
        QVBoxLayout* const s = add_section(tr("Animation"));
        animate_box = Check(s, tr("Animate the background"),
                            [](bool on) { UISettings::values.home_anim_enabled = on; });
        animation_options = new QWidget;
        auto* const a = new QVBoxLayout(animation_options);
        a->setContentsMargins(0, 0, 0, 0);
        a->setSpacing(6);
        rays_box = Check(a, tr("Sunbeams"), [](bool on) { UISettings::values.home_anim_rays = on; });
        a->addWidget(new QLabel(tr("Particles")));
        particles_group =
            Segments(a, {tr("None"), tr("Dust"), tr("Embers"), tr("Both")}, [this](int id) {
                UISettings::values.home_anim_particles = static_cast<u32>(id);
                Changed();
            });
        a->addWidget(new QLabel(tr("Particle motion")));
        motion_group =
            Segments(a, {tr("Natural"), tr("Rise"), tr("Fall"), tr("Swirl")}, [this](int id) {
                UISettings::values.home_anim_motion = static_cast<u32>(id);
                Changed();
            });
        amount_slider = LabelledSlider(
            a, tr("Amount"), 25, 200, [](int v) { return QStringLiteral("%1%").arg(v); },
            [](int v) { UISettings::values.home_anim_amount = static_cast<u32>(v); },
            &amount_label);
        speed_slider = LabelledSlider(
            a, tr("Speed"), 25, 300, [](int v) { return QStringLiteral("%1%").arg(v); },
            [](int v) { UISettings::values.home_anim_speed = static_cast<u32>(v); }, &speed_label);
        glow_box = Check(a, tr("Glow in the picked game's colours"),
                         [](bool on) { UISettings::values.home_anim_game_glow = on; });
        pulse_box = Check(a, tr("Breathing warm glow"),
                          [](bool on) { UISettings::values.home_anim_pulse = on; });
        vignette_box = Check(a, tr("Darken the edges"),
                             [](bool on) { UISettings::values.home_anim_vignette = on; });
        s->addWidget(animation_options);
    }

    // --- Games ---
    {
        QVBoxLayout* const s = add_section(tr("Games"));
        s->addWidget(new QLabel(tr("Display mode")));
        mode_group = Segments(s, {tr("List"), tr("Grid"), tr("Home")}, [this](int id) {
            if (set_mode) {
                set_mode(static_cast<Settings::GameListMode>(id));
            }
            Changed();
        });
        details_box = Check(s, tr("Show the picked game's details beside the games"),
                            [](bool on) { UISettings::values.show_game_details = on; });
        s->addWidget(new QLabel(tr("Game cards")));
        cards_group = Segments(s, {tr("Plain"), tr("Badge"), tr("Cartridge")}, [this](int id) {
            UISettings::values.game_card_style = static_cast<u32>(id);
            Changed();
        });

        // The home view's carousel.
        carousel_options = new QWidget;
        auto* const c = new QVBoxLayout(carousel_options);
        c->setContentsMargins(0, 4, 0, 0);
        c->setSpacing(6);
        c->addWidget(new QLabel(tr("Home layout")));
        layout_group = Segments(c, {tr("Stack"), tr("Row")}, [this](int id) {
            UISettings::values.carousel_layout = static_cast<u32>(id);
            Changed();
        });
        stack_options = new QWidget;
        auto* const st = new QVBoxLayout(stack_options);
        st->setContentsMargins(0, 0, 0, 0);
        st->setSpacing(6);
        st->addWidget(new QLabel(tr("Card size")));
        card_size_group = Segments(st, {tr("Full"), QStringLiteral("3/4"), QStringLiteral("2/3"),
                                        QStringLiteral("1/2"), QStringLiteral("1/3")},
                                   [this](int id) {
                                       UISettings::values.carousel_card_size = static_cast<u32>(id);
                                       Changed();
                                   });
        st->addWidget(new QLabel(tr("Cards per side")));
        side_count_group = Segments(st, {QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"),
                                         QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")},
                                    [this](int id) {
                                        UISettings::values.carousel_side_count =
                                            static_cast<u32>(id + 3);
                                        Changed();
                                    });
        c->addWidget(stack_options);
        banner_box = Check(c, tr("Show the picked game's picture as the background"),
                           [](bool on) { UISettings::values.carousel_banner_background = on; });
        s->addWidget(carousel_options);

        // The list and the grid.
        icon_options = new QWidget;
        auto* const ic = new QVBoxLayout(icon_options);
        ic->setContentsMargins(0, 4, 0, 0);
        ic->addWidget(new QLabel(tr("Icon size")));
        icon_size_group = Segments(ic, {tr("Off"), QStringLiteral("32"), QStringLiteral("64"),
                                        QStringLiteral("128"), QStringLiteral("256")},
                                   [this](int id) {
                                       static constexpr std::array<u32, 5> sizes{0, 32, 64, 128, 256};
                                       if (set_icon_size) {
                                           set_icon_size(sizes[static_cast<size_t>(id)]);
                                       }
                                       Changed();
                                   });
        s->addWidget(icon_options);
    }

    // --- Sounds ---
    {
        QVBoxLayout* const s = add_section(tr("Home screen sounds"));
        music_box = Check(s, tr("Play the picked game's music"),
                          [](bool on) { UISettings::values.menu_game_music = on; });
        random_box = Check(s, tr("Soundtrack mode (random songs)"),
                           [](bool on) { UISettings::values.menu_music_random = on; });
        hum_box = Check(s, tr("Ambient warmth hum"),
                        [](bool on) { UISettings::values.menu_ambient = on; });
        clicks_box = Check(s, tr("Navigation sound effects"),
                           [](bool on) { UISettings::values.menu_sounds = on; });
        mute_box = Check(s, tr("Mute all home screen sounds"),
                         [](bool on) { UISettings::values.menu_muted = on; });
    }

    auto* const reset = new QPushButton(tr("Reset to Defaults"));
    connect(reset, &QPushButton::clicked, this, &CustomizePanel::ResetToDefaults);
    layout->addSpacing(8);
    layout->addWidget(reset);
    layout->addStretch(1);

    Refresh();
}

QWidget* CustomizePanel::Section(const QString& title) {
    auto* const section = new QWidget;
    auto* const layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 10, 0, 4);
    layout->setSpacing(6);
    auto* const label = new QLabel(title);
    label->setObjectName(QStringLiteral("CustomizeSectionTitle"));
    layout->addWidget(label);
    return section;
}

QButtonGroup* CustomizePanel::Segments(QVBoxLayout* layout, const std::vector<QString>& names,
                                       const std::function<void(int)>& picked) {
    auto* const row = new QHBoxLayout;
    row->setSpacing(4);
    auto* const group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int id = 0; id < static_cast<int>(names.size()); ++id) {
        auto* const button = new QPushButton(names[static_cast<size_t>(id)]);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("CustomizeSegment"));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setMinimumWidth(0);
        group->addButton(button, id);
        row->addWidget(button);
    }
    connect(group, &QButtonGroup::idClicked, this, [this, picked](int id) {
        if (!refreshing) {
            picked(id);
        }
    });
    layout->addLayout(row);
    return group;
}

void CustomizePanel::ColourSwatches(QVBoxLayout* layout, ColourRow& row,
                                    const std::vector<Swatch>& presets, const QString& title) {
    auto* const grid = new QGridLayout;
    grid->setSpacing(2);
    const auto make_button = [](const QString& name) {
        auto* const button = new QToolButton;
        button->setObjectName(QStringLiteral("CustomizeSwatch"));
        button->setIconSize(QSize(SwatchSize, SwatchSize));
        button->setFixedSize(SwatchSize + 10, SwatchSize + 10);
        button->setToolTip(name);
        button->setAutoRaise(true);
        return button;
    };
    int index = 0;
    for (const Swatch& preset : presets) {
        QToolButton* const button = make_button(preset.name);
        grid->addWidget(button, index / SwatchesPerRow, index % SwatchesPerRow);
        row.buttons.emplace_back(button, preset.color);
        // The default is stored as "nothing chosen", so a later change of default follows.
        const bool is_default = preset.color == row.fallback;
        connect(button, &QToolButton::clicked, this, [this, &row, color = preset.color, is_default] {
            row.set(is_default ? std::string{} : color.name(QColor::HexRgb).toStdString());
            Changed(true);
        });
        ++index;
    }
    row.custom = make_button(tr("Custom..."));
    grid->addWidget(row.custom, index / SwatchesPerRow, index % SwatchesPerRow);
    connect(row.custom, &QToolButton::clicked, this, [this, &row, title] {
        const QColor chosen = QColorDialog::getColor(Current(row.get(), row.fallback), this, title);
        if (chosen.isValid()) {
            row.set(chosen.name(QColor::HexRgb).toStdString());
            Changed(true);
        }
    });
    layout->addLayout(grid);
}

QSlider* CustomizePanel::LabelledSlider(QVBoxLayout* layout, const QString& name, int min, int max,
                                        const std::function<QString(int)>& text,
                                        const std::function<void(int)>& changed,
                                        QLabel** value_label) {
    auto* const top = new QHBoxLayout;
    top->addWidget(new QLabel(name), 1);
    auto* const value = new QLabel;
    top->addWidget(value);
    layout->addLayout(top);
    auto* const slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    layout->addWidget(slider);
    connect(slider, &QSlider::valueChanged, this, [this, value, text, changed](int v) {
        value->setText(text(v));
        if (!refreshing) {
            changed(v);
            Changed();
        }
    });
    *value_label = value;
    return slider;
}

QCheckBox* CustomizePanel::Check(QVBoxLayout* layout, const QString& name,
                                 const std::function<void(bool)>& changed) {
    auto* const box = new QCheckBox(name);
    connect(box, &QCheckBox::toggled, this, [this, changed](bool on) {
        if (!refreshing) {
            changed(on);
            Changed();
        }
    });
    layout->addWidget(box);
    return box;
}

QPushButton* CustomizePanel::ColourButton(const QString& title,
                                          const std::function<std::string()>& get,
                                          const std::function<void(const std::string&)>& set) {
    auto* const button = new QPushButton;
    button->setIconSize(QSize(SwatchSize, SwatchSize));
    connect(button, &QPushButton::clicked, this, [this, title, get, set] {
        const QColor chosen = QColorDialog::getColor(Current(get(), Qt::black), this, title);
        if (chosen.isValid()) {
            set(chosen.name(QColor::HexRgb).toStdString());
            Changed();
        }
    });
    colour_buttons.emplace_back(button, get);
    return button;
}

void CustomizePanel::ChoosePicture() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Home Background Picture"), {},
        tr("Pictures (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All Files (*.*)"));
    if (!path.isEmpty()) {
        UsePicture(path);
    }
}

void CustomizePanel::UsePicture(const QString& path) {
    if (Thumbnail(path, QSize(16, 16)).isNull()) {
        return; // not a picture Qt can read
    }
    UISettings::values.home_bg_picture = path.toStdString();
    UISettings::values.home_bg_kind = 1;
    QStringList recent = RecentList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > RecentPictures) {
        recent.removeLast();
    }
    UISettings::values.home_bg_recent = recent.join(QLatin1Char('|')).toStdString();
    Changed();
}

void CustomizePanel::ResetToDefaults() {
    auto& v = UISettings::values;
    v.theme = "phoenix";
    for (auto* setting : {&v.theme_accent, &v.theme_background, &v.home_bg_color,
                          &v.home_bg_color2}) {
        ResetSetting(*setting);
    }
    ResetSetting(v.theme_shape);
    for (auto* setting : {&v.home_bg_kind, &v.home_bg_angle,
                          &v.home_anim_particles, &v.home_anim_motion, &v.home_anim_amount,
                          &v.home_anim_speed, &v.game_card_style, &v.carousel_layout,
                          &v.carousel_card_size, &v.carousel_side_count}) {
        ResetSetting(*setting);
    }
    for (auto* setting : {&v.carousel_banner_background, &v.home_anim_enabled, &v.home_anim_rays,
                          &v.home_anim_game_glow,
                          &v.home_anim_pulse, &v.home_anim_vignette}) {
        ResetSetting(*setting);
    }
    // The picture and the recent list stay, ready for a switch back to Picture.
    Changed(true);
}

void CustomizePanel::Changed(bool restyle) {
    if (apply) {
        apply(restyle);
    }
    Refresh();
}

void CustomizePanel::Refresh() {
    refreshing = true;
    const auto& v = UISettings::values;

    for (int index = 0; index < theme_box->count(); ++index) {
        if (theme_box->itemData(index).toString().toStdString() == v.theme) {
            theme_box->setCurrentIndex(index);
        }
    }
    // The colours and the button shape belong to the PhoenixEmu theme.
    const bool phoenix = v.theme == "phoenix";
    theme_colours->setEnabled(phoenix);
    theme_colours->setToolTip(phoenix ? QString{} : tr("For the PhoenixEmu theme"));
    for (ColourRow* const row : {&accent_row, &background_row}) {
        const QColor current = Current(row->get(), row->fallback);
        bool matched = false;
        for (const auto& [button, color] : row->buttons) {
            const bool picked = color == current;
            matched = matched || picked;
            button->setIcon(Chip(color, picked));
        }
        row->custom->setIcon(CustomChip(current, !matched));
    }
    shape_group->button(static_cast<int>(std::min(v.theme_shape.GetValue(), 2U)))->setChecked(true);

    const int kind = static_cast<int>(std::min(v.home_bg_kind.GetValue(), 3U));
    kind_group->button(kind)->setChecked(true);
    picture_page->setVisible(kind == 1);
    solid_page->setVisible(kind == 2);
    gradient_page->setVisible(kind == 3);

    const QString picture = QString::fromStdString(v.home_bg_picture.GetValue());
    const QPixmap preview = picture.isEmpty() ? QPixmap{} : Thumbnail(picture, PreviewSize);
    if (preview.isNull()) {
        picture_preview->setPixmap({});
        picture_preview->setText(picture.isEmpty() ? tr("No picture chosen")
                                                   : tr("The picture could not be opened"));
    } else {
        picture_preview->setPixmap(preview);
    }
    // The recent row: a few small buttons, made again only when the pictures change.
    const QString recent_key = picture + QLatin1Char('\n') +
                               QString::fromStdString(v.home_bg_recent.GetValue());
    QLayout* const recent_layout = recent_row->layout();
    const bool rebuild_recent = recent_key != shown_recent;
    shown_recent = recent_key;
    while (rebuild_recent && recent_layout->count() > 0) {
        QLayoutItem* const item = recent_layout->takeAt(0);
        delete item->widget();
        delete item;
    }
    for (const QString& path : rebuild_recent ? RecentList() : QStringList{}) {
        const QPixmap thumb = Thumbnail(path, QSize(64, 40));
        if (thumb.isNull()) {
            continue;
        }
        auto* const button = new QToolButton;
        button->setIcon(QIcon{thumb});
        button->setIconSize(QSize(64, 40));
        button->setToolTip(path);
        button->setCheckable(true);
        button->setChecked(path == picture);
        connect(button, &QToolButton::clicked, this, [this, path] { UsePicture(path); });
        recent_layout->addWidget(button);
    }
    if (rebuild_recent) {
        static_cast<QHBoxLayout*>(recent_layout)->addStretch(1);
    }

    for (const auto& [button, get] : colour_buttons) {
        const QColor color = Current(get(), Qt::black);
        button->setIcon(Chip(color, false));
        button->setText(color.name(QColor::HexRgb).toUpper());
    }
    angle_slider->setValue(static_cast<int>(v.home_bg_angle.GetValue()));
    angle_label->setText(QStringLiteral("%1°").arg(angle_slider->value()));

    animate_box->setChecked(v.home_anim_enabled.GetValue());
    rays_box->setChecked(v.home_anim_rays.GetValue());
    particles_group->button(static_cast<int>(std::min(v.home_anim_particles.GetValue(), 3U)))
        ->setChecked(true);
    motion_group->button(static_cast<int>(std::min(v.home_anim_motion.GetValue(), 3U)))
        ->setChecked(true);
    amount_slider->setValue(static_cast<int>(v.home_anim_amount.GetValue()));
    amount_label->setText(QStringLiteral("%1%").arg(amount_slider->value()));
    speed_slider->setValue(static_cast<int>(v.home_anim_speed.GetValue()));
    speed_label->setText(QStringLiteral("%1%").arg(speed_slider->value()));
    glow_box->setChecked(v.home_anim_game_glow.GetValue());
    pulse_box->setChecked(v.home_anim_pulse.GetValue());
    vignette_box->setChecked(v.home_anim_vignette.GetValue());
    // Particle motion means nothing without particles.
    for (QAbstractButton* const button : motion_group->buttons()) {
        button->setEnabled(v.home_anim_particles.GetValue() != 0);
    }

    mode_group->button(static_cast<int>(v.game_list_mode.GetValue()))->setChecked(true);
    cards_group->button(static_cast<int>(std::min(v.game_card_style.GetValue(), 2U)))
        ->setChecked(true);
    const bool home = v.game_list_mode.GetValue() == Settings::GameListMode::CarouselView;
    carousel_options->setVisible(home);
    icon_options->setVisible(!home);
    layout_group->button(static_cast<int>(std::min(v.carousel_layout.GetValue(), 1U)))
        ->setChecked(true);
    stack_options->setVisible(v.carousel_layout.GetValue() == 0);
    card_size_group->button(static_cast<int>(std::min(v.carousel_card_size.GetValue(), 4U)))
        ->setChecked(true);
    side_count_group
        ->button(static_cast<int>(std::clamp(v.carousel_side_count.GetValue(), 3U, 8U) - 3))
        ->setChecked(true);
    banner_box->setChecked(v.carousel_banner_background.GetValue());
    details_box->setChecked(v.show_game_details.GetValue());
    static constexpr std::array<u32, 5> icon_sizes{0, 32, 64, 128, 256};
    const auto icon_it = std::find(icon_sizes.begin(), icon_sizes.end(),
                                   v.game_icon_size.GetValue());
    if (icon_it != icon_sizes.end()) {
        icon_size_group->button(static_cast<int>(icon_it - icon_sizes.begin()))->setChecked(true);
    }

    music_box->setChecked(v.menu_game_music.GetValue());
    random_box->setChecked(v.menu_music_random.GetValue());
    random_box->setEnabled(v.menu_game_music.GetValue());
    hum_box->setChecked(v.menu_ambient.GetValue());
    clicks_box->setChecked(v.menu_sounds.GetValue());
    mute_box->setChecked(v.menu_muted.GetValue());
    refreshing = false;
}
