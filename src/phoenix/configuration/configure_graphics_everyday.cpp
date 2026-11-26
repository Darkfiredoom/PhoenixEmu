// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>

#include "common/settings.h"
#include "phoenix/configuration/configure_graphics_everyday.h"
#include "phoenix/configuration/settings_shell.h"

namespace {

using Settings::ResolutionSetup;
using Settings::ScalingFilter;

/// A preset card: its name and what it means, an ember edge and a tick when it is the one.
class PresetCard final : public QAbstractButton {
public:
    PresetCard(const QString& name_, const QString& detail_, QWidget* parent)
        : QAbstractButton(parent), name{name_}, detail{detail_} {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(74);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (isChecked()) {
            QLinearGradient fill{r.topLeft(), r.bottomRight()};
            fill.setColorAt(0.0, QColor(0x4A, 0x22, 0x14));
            fill.setColorAt(1.0, QColor(0x2A, 0x17, 0x10));
            p.setBrush(fill);
            p.setPen(QPen(QColor(0xFF, 0x5A, 0x1F), 1.5));
        } else {
            p.setBrush(QColor(0x1A, 0x15, 0x12));
            p.setPen(QPen(underMouse() ? QColor(0x5A, 0x44, 0x34) : QColor(0x2E, 0x24, 0x1D), 1));
        }
        p.drawRoundedRect(r, 14, 14);
        QFont title = font();
        title.setPixelSize(17);
        title.setWeight(QFont::Black);
        p.setFont(title);
        p.setPen(QColor(0xF6, 0xEB, 0xDD));
        p.drawText(QRectF(r.left() + 18, r.top() + 12, r.width() - 50, 24),
                   Qt::AlignLeft | Qt::AlignVCenter, name);
        QFont small = font();
        small.setPixelSize(13);
        p.setFont(small);
        p.setPen(isChecked() ? QColor(0xFF, 0xC9, 0xA8) : QColor(0xA8, 0x99, 0x8A));
        p.drawText(QRectF(r.left() + 18, r.top() + 38, r.width() - 24, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, detail);
        if (isChecked()) {
            const QPointF c{r.right() - 22, r.top() + 22};
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xFF, 0x5A, 0x1F));
            p.drawEllipse(c, 10, 10);
            p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(c + QPointF(-4.5, 0), c + QPointF(-1, 3.5));
            p.drawLine(c + QPointF(-1, 3.5), c + QPointF(5, -3.5));
        }
    }

private:
    QString name;
    QString detail;
};

/// An on/off switch.
class Switch final : public QAbstractButton {
public:
    explicit Switch(QWidget* parent) : QAbstractButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(46, 26);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? QColor(0xFF, 0x5A, 0x1F) : QColor(0x3A, 0x2E, 0x25));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        const qreal d = r.height() - 6;
        const qreal x = isChecked() ? r.right() - 3 - d : r.left() + 3;
        p.setBrush(isChecked() ? QColor(Qt::white) : QColor(0xB8, 0xA8, 0x98));
        p.drawEllipse(QRectF(x, r.top() + 3, d, d));
    }
};

QLabel* IconTile(SettingsShell::Icon icon, QWidget* parent) {
    auto* tile = new QLabel(parent);
    tile->setFixedSize(36, 36);
    tile->setAlignment(Qt::AlignCenter);
    tile->setPixmap(SettingsShell::MakeIcon(icon).pixmap(18, 18));
    tile->setStyleSheet(QStringLiteral("background: #2A211B; border-radius: 9px;"));
    return tile;
}

/// The Switch's own resolution times the scale, docked (1080p), as "1620p".
QString DockedHeight(ResolutionSetup setup) {
    static constexpr std::pair<ResolutionSetup, double> scales[] = {
        {ResolutionSetup::Res1_4X, 0.25}, {ResolutionSetup::Res1_2X, 0.5},
        {ResolutionSetup::Res3_4X, 0.75}, {ResolutionSetup::Res1X, 1.0},
        {ResolutionSetup::Res5_4X, 1.25}, {ResolutionSetup::Res3_2X, 1.5},
        {ResolutionSetup::Res2X, 2.0},    {ResolutionSetup::Res3X, 3.0},
        {ResolutionSetup::Res4X, 4.0},    {ResolutionSetup::Res5X, 5.0},
        {ResolutionSetup::Res6X, 6.0},    {ResolutionSetup::Res7X, 7.0},
        {ResolutionSetup::Res8X, 8.0}};
    for (const auto& [value, scale] : scales) {
        if (value == setup) {
            return QStringLiteral("%1p").arg(static_cast<int>(1080 * scale));
        }
    }
    return {};
}

} // Anonymous namespace

ConfigureGraphicsEveryday::ConfigureGraphicsEveryday(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("GraphicsEveryday"));
    setStyleSheet(QStringLiteral(R"(
        QWidget#GraphicsEveryday QLabel { background: transparent; }
        QLabel#EverydaySection { color: #A8998A; font-size: 11px; font-weight: 800; letter-spacing: 1px; }
        QLabel#EverydayName { color: #F6EBDD; font-size: 14px; font-weight: 800; }
        QLabel#EverydayHint { color: #A8998A; font-size: 12px; }
        QFrame#EverydayGroup { background: #15110E; border: 1px solid #2A211B; border-radius: 14px; }
        QFrame#EverydayLine { background: #2A211B; max-height: 1px; min-height: 1px; border: none; }
        QWidget#GraphicsEveryday QComboBox { min-width: 190px; }
    )"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("GraphicsEverydayBody"));
    body->setStyleSheet(QStringLiteral("QWidget#GraphicsEverydayBody { background: transparent; }"));
    scroll->setWidget(body);
    scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 6, 12, 12);
    layout->setSpacing(10);

    // The presets.
    auto* preset_row = new QHBoxLayout;
    preset_row->setSpacing(12);
    preset_group = new QButtonGroup(this);
    preset_group->setExclusive(true);
    const std::array<std::pair<QString, QString>, 4> preset_text{{
        {tr("Low"), tr("0.75× · fastest")},
        {tr("Native"), tr("1× · as on Switch")},
        {tr("High"), tr("1.5× · FSR · recommended")},
        {tr("Ultra"), tr("2× · needs a strong GPU")},
    }};
    for (int i = 0; i < 4; ++i) {
        auto* card = new PresetCard(preset_text[i].first, preset_text[i].second, body);
        presets[i] = card;
        preset_group->addButton(card, i);
        preset_row->addWidget(card);
    }
    layout->addLayout(preset_row);

    const auto section = [&](const QString& text) {
        auto* label = new QLabel(text, body);
        label->setObjectName(QStringLiteral("EverydaySection"));
        layout->addSpacing(8);
        layout->addWidget(label);
    };
    QFrame* group = nullptr;
    QVBoxLayout* group_layout = nullptr;
    const auto begin_group = [&] {
        group = new QFrame(body);
        group->setObjectName(QStringLiteral("EverydayGroup"));
        group_layout = new QVBoxLayout(group);
        group_layout->setContentsMargins(16, 4, 16, 4);
        group_layout->setSpacing(0);
        layout->addWidget(group);
    };
    const auto add_row = [&](SettingsShell::Icon icon, const QString& name, const QString& hint,
                             QWidget* control, bool line_before) -> QLabel* {
        if (line_before) {
            auto* line = new QFrame(group);
            line->setObjectName(QStringLiteral("EverydayLine"));
            group_layout->addWidget(line);
        }
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 12, 0, 12);
        row->setSpacing(14);
        row->addWidget(IconTile(icon, group));
        auto* text = new QVBoxLayout;
        text->setSpacing(1);
        auto* name_label = new QLabel(name, group);
        name_label->setObjectName(QStringLiteral("EverydayName"));
        auto* hint_label = new QLabel(hint, group);
        hint_label->setObjectName(QStringLiteral("EverydayHint"));
        hint_label->setWordWrap(true);
        text->addWidget(name_label);
        text->addWidget(hint_label);
        row->addLayout(text, 1);
        control->setParent(group);
        row->addWidget(control, 0, Qt::AlignVCenter);
        group_layout->addLayout(row);
        return hint_label;
    };

    // The picture.
    section(tr("PICTURE"));
    begin_group();
    resolution = new QComboBox;
    const std::pair<QString, ResolutionSetup> scales[] = {
        {tr("0.5× · 540p"), ResolutionSetup::Res1_2X},  {tr("0.75× · 810p"), ResolutionSetup::Res3_4X},
        {tr("1× · 1080p"), ResolutionSetup::Res1X},     {tr("1.25× · 1350p"), ResolutionSetup::Res5_4X},
        {tr("1.5× · 1620p"), ResolutionSetup::Res3_2X}, {tr("2× · 2160p"), ResolutionSetup::Res2X},
        {tr("3× · 3240p"), ResolutionSetup::Res3X},     {tr("4× · 4320p"), ResolutionSetup::Res4X},
    };
    for (const auto& [name, value] : scales) {
        resolution->addItem(name, static_cast<int>(value));
    }
    resolution_hint = add_row(SettingsShell::Icon::Graphics, tr("Resolution"), QString{}, resolution, false);
    upscaling = new QComboBox;
    const std::pair<QString, ScalingFilter> filters[] = {
        {tr("AMD FSR"), ScalingFilter::Fsr},
        {tr("AMD FSR 3"), ScalingFilter::Fsr3},
        {tr("Snapdragon GSR"), ScalingFilter::Sgsr},
        {tr("Apex neural"), ScalingFilter::Anr},
        {tr("ScaleForce"), ScalingFilter::ScaleForce},
        {tr("Lanczos"), ScalingFilter::Lanczos},
        {tr("Bicubic"), ScalingFilter::Bicubic},
        {tr("Bilinear"), ScalingFilter::Bilinear},
        {tr("Nearest (sharp pixels)"), ScalingFilter::NearestNeighbor},
        {tr("Area"), ScalingFilter::Area},
    };
    for (const auto& [name, value] : filters) {
        upscaling->addItem(name, static_cast<int>(value));
    }
    add_row(SettingsShell::Icon::Artwork, tr("Upscaling"),
            tr("Sharpens the picture after it's enlarged"), upscaling, true);
    anti_aliasing = new QComboBox;
    anti_aliasing->addItem(tr("Off"), static_cast<int>(Settings::AntiAliasing::None));
    anti_aliasing->addItem(QStringLiteral("FXAA"), static_cast<int>(Settings::AntiAliasing::Fxaa));
    anti_aliasing->addItem(QStringLiteral("SMAA"), static_cast<int>(Settings::AntiAliasing::Smaa));
    add_row(SettingsShell::Icon::Overview, tr("Anti-aliasing"), tr("Smooths jagged edges"),
            anti_aliasing, true);

    // Smoothness.
    section(tr("SMOOTHNESS"));
    begin_group();
    frame_gen = new QComboBox;
    frame_gen->addItem(tr("Off"), -1);
    frame_gen->addItem(tr("Optical Flow ×2"), static_cast<int>(Settings::FrameGenType::Fsr3));
    frame_gen->addItem(tr("AMD FSR 3.1 ×2"), static_cast<int>(Settings::FrameGenType::AmdFsr));
    frame_gen->addItem(tr("Apex neural ×2"), static_cast<int>(Settings::FrameGenType::Anr));
    frame_gen->addItem(tr("Lossless ×2"), static_cast<int>(Settings::FrameGenType::Lsfg));
    add_row(SettingsShell::Icon::FrameRate, tr("Frame generation"),
            tr("Adds frames between the game's own, for a 60 FPS feel"), frame_gen, false);
    vsync = new Switch(nullptr);
    add_row(SettingsShell::Icon::Overview, tr("VSync"),
            tr("No tearing, at the cost of a little delay"), vsync, true);
    docked = new Switch(nullptr);
    add_row(SettingsShell::Icon::Controls, tr("Docked mode"),
            tr("Games run as on a TV; off is handheld"), docked, true);
    layout->addStretch(1);

    connect(preset_group, &QButtonGroup::idClicked, this, [this](int id) {
        static constexpr ResolutionSetup preset_scales[] = {
            ResolutionSetup::Res3_4X, ResolutionSetup::Res1X, ResolutionSetup::Res3_2X,
            ResolutionSetup::Res2X};
        resolution->setCurrentIndex(resolution->findData(static_cast<int>(preset_scales[id])));
        touched_resolution = true;
        // Low, High and Ultra enlarge with FSR (Apex neural stays if it was picked).
        if (id != 1 && upscaling->currentData().toInt() != static_cast<int>(ScalingFilter::Anr)) {
            upscaling->setCurrentIndex(upscaling->findData(static_cast<int>(ScalingFilter::Fsr)));
            touched_upscaling = true;
        }
        UpdateResolutionHint();
    });
    connect(resolution, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading) {
            touched_resolution = true;
        }
        UpdatePresets();
        UpdateResolutionHint();
    });
    connect(upscaling, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading) {
            touched_upscaling = true;
        }
    });
    connect(anti_aliasing, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading) {
            touched_anti_aliasing = true;
        }
    });
    connect(frame_gen, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading) {
            touched_frame_gen = true;
        }
    });
    connect(vsync, &QAbstractButton::toggled, this, [this] {
        if (!loading) {
            touched_vsync = true;
        }
    });
    connect(docked, &QAbstractButton::toggled, this, [this] {
        if (!loading) {
            touched_docked = true;
        }
    });

    SetConfiguration();
}

void ConfigureGraphicsEveryday::SetConfiguration() {
    loading = true;
    const auto setup = Settings::values.resolution_setup.GetValue();
    int index = resolution->findData(static_cast<int>(setup));
    if (index < 0) {
        resolution->addItem(tr("Custom"), static_cast<int>(setup));
        index = resolution->count() - 1;
    }
    resolution->setCurrentIndex(index);
    const auto filter = Settings::values.scaling_filter.GetValue();
    index = upscaling->findData(static_cast<int>(filter));
    if (index < 0) {
        upscaling->addItem(tr("Other"), static_cast<int>(filter));
        index = upscaling->count() - 1;
    }
    upscaling->setCurrentIndex(index);
    anti_aliasing->setCurrentIndex(
        std::max(0, anti_aliasing->findData(static_cast<int>(Settings::values.anti_aliasing.GetValue()))));
    const int fg = Settings::values.frame_gen.GetValue()
                       ? static_cast<int>(Settings::values.frame_gen_type.GetValue())
                       : -1;
    frame_gen->setCurrentIndex(std::max(0, frame_gen->findData(fg)));
    vsync->setChecked(Settings::values.vsync_mode.GetValue() != Settings::VSyncMode::Immediate);
    docked->setChecked(Settings::values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked);
    touched_resolution = touched_upscaling = touched_anti_aliasing = false;
    touched_frame_gen = touched_vsync = touched_docked = false;
    loading = false;
    UpdatePresets();
    UpdateResolutionHint();
}

void ConfigureGraphicsEveryday::UpdatePresets() {
    const int scale = resolution->currentData().toInt();
    static constexpr ResolutionSetup preset_scales[] = {
        ResolutionSetup::Res3_4X, ResolutionSetup::Res1X, ResolutionSetup::Res3_2X,
        ResolutionSetup::Res2X};
    preset_group->setExclusive(false);
    for (int i = 0; i < 4; ++i) {
        presets[i]->setChecked(static_cast<int>(preset_scales[i]) == scale);
    }
    preset_group->setExclusive(true);
}

void ConfigureGraphicsEveryday::UpdateResolutionHint() {
    const auto setup = static_cast<ResolutionSetup>(resolution->currentData().toInt());
    const QString height = DockedHeight(setup);
    resolution_hint->setText(height.isEmpty()
                                 ? tr("How sharp the game is drawn")
                                 : tr("Drawn at %1 when docked; takes effect when a game starts")
                                       .arg(height));
}

void ConfigureGraphicsEveryday::ApplyConfiguration() {
    if (touched_resolution) {
        Settings::values.resolution_setup.SetValue(
            static_cast<ResolutionSetup>(resolution->currentData().toInt()));
    }
    if (touched_upscaling) {
        Settings::values.scaling_filter.SetValue(
            static_cast<ScalingFilter>(upscaling->currentData().toInt()));
    }
    if (touched_anti_aliasing) {
        Settings::values.anti_aliasing.SetValue(
            static_cast<Settings::AntiAliasing>(anti_aliasing->currentData().toInt()));
    }
    if (touched_frame_gen) {
        const int type = frame_gen->currentData().toInt();
        Settings::values.frame_gen.SetValue(type >= 0);
        if (type >= 0) {
            Settings::values.frame_gen_type.SetValue(static_cast<Settings::FrameGenType>(type));
            // Apex neural frame generation works with its own upscaler.
            if (type == static_cast<int>(Settings::FrameGenType::Anr)) {
                Settings::values.scaling_filter.SetValue(ScalingFilter::Anr);
            }
        }
    }
    if (touched_vsync) {
        Settings::values.vsync_mode.SetValue(vsync->isChecked() ? Settings::VSyncMode::Fifo
                                                                : Settings::VSyncMode::Immediate);
    }
    if (touched_docked) {
        Settings::values.use_docked_mode.SetValue(docked->isChecked() ? Settings::ConsoleMode::Docked
                                                                      : Settings::ConsoleMode::Handheld);
    }
    touched_resolution = touched_upscaling = touched_anti_aliasing = false;
    touched_frame_gen = touched_vsync = touched_docked = false;
}
