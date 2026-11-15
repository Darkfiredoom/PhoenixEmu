// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "phoenix/customize_dialog.h"
#include "phoenix/util/phoenix_theme.h"
#include "qt_common/config/uisettings.h"

namespace {

constexpr int SwatchSize = 40;
constexpr int SwatchesPerRow = 6;

/// A round colour chip; `ring` draws the picked one's outline.
QIcon Chip(const QColor& color, bool ring) {
    QPixmap pixmap(SwatchSize * 2, SwatchSize * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    if (ring) {
        painter.setPen(QPen(QColor(255, 255, 255, 230), 2.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(2, 2, SwatchSize - 4, SwatchSize - 4));
    }
    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.setBrush(color);
    painter.drawEllipse(QRectF(6, 6, SwatchSize - 12, SwatchSize - 12));
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
    painter.drawEllipse(QRectF(6, 6, SwatchSize - 12, SwatchSize - 12));
    return QIcon{pixmap};
}

QColor Current(const std::string& setting, const QColor& fallback) {
    return setting.empty() ? fallback : QColor(QString::fromStdString(setting));
}

struct ThemeChoice {
    const char* key;
    const char* name;
};
constexpr std::array<ThemeChoice, 4> Themes{{
    {"phoenix", QT_TRANSLATE_NOOP("CustomizeDialog", "PhoenixEmu")},
    {"colorful_dark", QT_TRANSLATE_NOOP("CustomizeDialog", "Classic (Dark Colorful)")},
    {"colorful_midnight_blue", QT_TRANSLATE_NOOP("CustomizeDialog", "Classic Midnight Blue")},
    {"default", QT_TRANSLATE_NOOP("CustomizeDialog", "System (follows your desktop)")},
}};

} // Anonymous namespace

std::vector<CustomizeDialog::Swatch> CustomizeDialog::AccentPresets() {
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

std::vector<CustomizeDialog::Swatch> CustomizeDialog::BackgroundPresets() {
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

CustomizeDialog::CustomizeDialog(std::function<void()> apply_, QWidget* parent)
    : QDialog(parent), apply{std::move(apply_)} {
    setWindowTitle(tr("Appearance"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    theme = old_theme = UISettings::values.theme;
    accent = old_accent = UISettings::values.theme_accent.GetValue();
    background = old_background = UISettings::values.theme_background.GetValue();
    shape = old_shape = (std::min)(UISettings::values.theme_shape.GetValue(), 2U);

    auto* const layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto* const hint = new QLabel(tr("Changes show right away. Cancel puts everything back."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* const theme_row = new QHBoxLayout;
    theme_row->addWidget(new QLabel(tr("Theme")));
    theme_box = new QComboBox;
    for (const ThemeChoice& choice : Themes) {
        theme_box->addItem(tr(choice.name), QString::fromLatin1(choice.key));
    }
    theme_row->addWidget(theme_box, 1);
    layout->addLayout(theme_row);
    connect(theme_box, &QComboBox::activated, this, [this](int index) {
        theme = theme_box->itemData(index).toString().toStdString();
        Apply();
    });

    accent_row.fallback = PhoenixTheme::DefaultAccent;
    background_row.fallback = PhoenixTheme::DefaultBackground;
    accent_group = MakeColourRow(tr("Accent colour"), accent_row, accent, AccentPresets());
    background_group =
        MakeColourRow(tr("Background colour"), background_row, background, BackgroundPresets());
    layout->addWidget(accent_group);
    layout->addWidget(background_group);

    shape_group = new QGroupBox(tr("Button shape"));
    auto* const shape_layout = new QHBoxLayout(shape_group);
    auto* const shapes = new QButtonGroup(this);
    const std::array<QString, 3> shape_names{tr("Rounded pills"), tr("Soft corners"),
                                             tr("Square")};
    for (u32 index = 0; index < shape_names.size(); ++index) {
        auto* const button = new QRadioButton(shape_names[index]);
        shapes->addButton(button, static_cast<int>(index));
        shape_layout->addWidget(button);
        shape_buttons.push_back(button);
    }
    shape_layout->addStretch(1);
    connect(shapes, &QButtonGroup::idClicked, this, [this](int id) {
        shape = static_cast<u32>(id);
        Apply();
    });
    layout->addWidget(shape_group);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* const reset = buttons->addButton(tr("Reset to defaults"),
                                                  QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, this, [this] {
        theme = "phoenix";
        accent.clear();
        background.clear();
        shape = 0;
        Apply();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    Refresh();
}

QGroupBox* CustomizeDialog::MakeColourRow(const QString& title, ColourRow& row,
                                          std::string& setting,
                                          const std::vector<Swatch>& presets) {
    row.setting = &setting;
    auto* const group = new QGroupBox(title);
    auto* const grid = new QGridLayout(group);
    grid->setSpacing(4);
    const auto make_button = [&](const QString& name) {
        auto* const button = new QToolButton;
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(SwatchSize, SwatchSize));
        button->setText(name);
        button->setAutoRaise(true);
        button->setMinimumWidth(92);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return button;
    };
    // Equal columns, whatever the names' lengths.
    for (int column = 0; column < SwatchesPerRow; ++column) {
        grid->setColumnStretch(column, 1);
        grid->setColumnMinimumWidth(column, 112);
    }
    int index = 0;
    for (const Swatch& preset : presets) {
        QToolButton* const button = make_button(preset.name);
        grid->addWidget(button, index / SwatchesPerRow, index % SwatchesPerRow);
        row.buttons.emplace_back(button, preset.color);
        // The default is stored as "nothing chosen", so a later change of default follows.
        const bool is_default = preset.color == row.fallback;
        connect(button, &QToolButton::clicked, this, [this, &row, color = preset.color,
                                                      is_default] {
            *row.setting = is_default ? std::string{} : color.name(QColor::HexRgb).toStdString();
            Apply();
        });
        ++index;
    }
    row.custom = make_button(tr("Custom..."));
    grid->addWidget(row.custom, index / SwatchesPerRow, index % SwatchesPerRow);
    connect(row.custom, &QToolButton::clicked, this, [this, &row, title] {
        const QColor chosen =
            QColorDialog::getColor(Current(*row.setting, row.fallback), this, title);
        if (chosen.isValid()) {
            *row.setting = chosen.name(QColor::HexRgb).toStdString();
            Apply();
        }
    });
    return group;
}

void CustomizeDialog::Apply() {
    UISettings::values.theme = theme;
    UISettings::values.theme_accent = accent;
    UISettings::values.theme_background = background;
    UISettings::values.theme_shape = shape;
    if (apply) {
        apply();
    }
    Refresh();
}

void CustomizeDialog::Refresh() {
    for (int index = 0; index < theme_box->count(); ++index) {
        if (theme_box->itemData(index).toString().toStdString() == theme) {
            theme_box->setCurrentIndex(index);
        }
    }
    // The colours and the shape belong to the PhoenixEmu theme.
    const bool phoenix = theme == "phoenix";
    for (QGroupBox* const group : {accent_group, background_group, shape_group}) {
        group->setEnabled(phoenix);
        group->setToolTip(phoenix ? QString{} : tr("For the PhoenixEmu theme"));
    }
    for (ColourRow* const row : {&accent_row, &background_row}) {
        const QColor current = Current(*row->setting, row->fallback);
        bool matched = false;
        for (const auto& [button, color] : row->buttons) {
            const bool picked = color == current;
            matched = matched || picked;
            button->setIcon(Chip(color, picked));
        }
        row->custom->setIcon(CustomChip(current, !matched));
    }
    for (std::size_t index = 0; index < shape_buttons.size(); ++index) {
        shape_buttons[index]->setChecked(index == shape);
    }
}

void CustomizeDialog::reject() {
    theme = old_theme;
    accent = old_accent;
    background = old_background;
    shape = old_shape;
    Apply();
    QDialog::reject();
}
