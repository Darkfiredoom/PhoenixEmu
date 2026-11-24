// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include "common/settings.h"
#include "input_common/main.h"
#include "phoenix/configuration/configure_input_player.h"
#include "phoenix/controllers_dialog.h"
#include "phoenix/util/phoenix_theme.h"

namespace {

enum class Layout { Xbox = 1, PlayStation = 2, Nintendo = 3 };

Layout LayoutFor(const QString& name) {
    if (name.contains(QStringLiteral("DualSense"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("DualShock"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("PS4"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("PS5"), Qt::CaseInsensitive)) {
        return Layout::PlayStation;
    }
    if (name.contains(QStringLiteral("Nintendo"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("Joy-Con"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("Pro Controller"), Qt::CaseInsensitive)) {
        return Layout::Nintendo;
    }
    return Layout::Xbox;
}

} // namespace

/// A generic gamepad, drawn: the face buttons labelled the way the pad in use labels them.
class ControllerArt final : public QWidget {
public:
    explicit ControllerArt(QWidget* parent) : QWidget(parent) {
        setMinimumSize(300, 190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void SetLayout(Layout value) {
        if (value != layout) {
            layout = value;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Laid out on a 300 x 190 canvas, scaled to fit.
        const qreal scale = std::min(width() / 300.0, height() / 190.0);
        p.translate((width() - 300 * scale) / 2, (height() - 190 * scale) / 2);
        p.scale(scale, scale);

        const QColor body = PhoenixTheme::Recolor(QColor(0x2E, 0x26, 0x1C));
        const QColor edge = PhoenixTheme::Recolor(QColor(0xFF, 0xC8, 0x3D));
        // Shoulders, then the body with its two grips.
        p.setPen(Qt::NoPen);
        p.setBrush(body.lighter(130));
        p.drawRoundedRect(QRectF(48, 22, 70, 22), 10, 10);
        p.drawRoundedRect(QRectF(182, 22, 70, 22), 10, 10);
        QPainterPath shape;
        shape.moveTo(70, 40);
        shape.cubicTo(110, 34, 190, 34, 230, 40);
        shape.cubicTo(270, 46, 292, 120, 280, 160);
        shape.cubicTo(272, 186, 240, 186, 226, 164);
        shape.cubicTo(212, 142, 200, 132, 150, 132);
        shape.cubicTo(100, 132, 88, 142, 74, 164);
        shape.cubicTo(60, 186, 28, 186, 20, 160);
        shape.cubicTo(8, 120, 30, 46, 70, 40);
        p.setBrush(body);
        p.setPen(QPen(edge, 2));
        p.drawPath(shape);

        // Sticks and the D-pad (a Nintendo pad has its left stick up top, as Xbox does).
        const bool sony = layout == Layout::PlayStation;
        const QPointF left_stick = sony ? QPointF(108, 112) : QPointF(76, 78);
        const QPointF dpad = sony ? QPointF(76, 78) : QPointF(108, 112);
        p.setPen(QPen(edge.darker(140), 2));
        p.setBrush(body.darker(140));
        p.drawEllipse(left_stick, 15, 15);
        p.drawEllipse(QPointF(192, 112), 15, 15);
        p.setPen(Qt::NoPen);
        p.setBrush(body.lighter(170));
        p.drawRoundedRect(QRectF(dpad.x() - 18, dpad.y() - 6, 36, 12), 3, 3);
        p.drawRoundedRect(QRectF(dpad.x() - 6, dpad.y() - 18, 12, 36), 3, 3);

        // Face buttons: top, right, bottom, left.
        const QPointF face{224, 78};
        const std::array<QPointF, 4> spots{QPointF(0, -18), QPointF(18, 0), QPointF(0, 18),
                                           QPointF(-18, 0)};
        std::array<QString, 4> labels;
        std::array<QColor, 4> colours;
        switch (layout) {
        case Layout::PlayStation:
            labels = {QString::fromUtf8("△"), QString::fromUtf8("○"),
                      QString::fromUtf8("✕"), QString::fromUtf8("□")};
            colours = {QColor(0x3D, 0xD6, 0xB5), QColor(0xFF, 0x5C, 0x6C), QColor(0x7A, 0xA8, 0xFF),
                       QColor(0xE8, 0x8C, 0xD8)};
            break;
        case Layout::Nintendo:
            labels = {QStringLiteral("X"), QStringLiteral("A"), QStringLiteral("B"),
                      QStringLiteral("Y")};
            colours.fill(QColor(0xE8, 0xE4, 0xDC));
            break;
        case Layout::Xbox:
            labels = {QStringLiteral("Y"), QStringLiteral("B"), QStringLiteral("A"),
                      QStringLiteral("X")};
            colours = {QColor(0xF2, 0xC2, 0x2E), QColor(0xE8, 0x4B, 0x3C), QColor(0x5D, 0xC2, 0x4E),
                       QColor(0x3D, 0x8B, 0xE8)};
            break;
        }
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(11);
        p.setFont(font);
        for (size_t i = 0; i < spots.size(); ++i) {
            const QPointF centre = face + spots[i];
            p.setPen(Qt::NoPen);
            p.setBrush(body.darker(150));
            p.drawEllipse(centre, 10, 10);
            p.setPen(colours[i]);
            p.drawText(QRectF(centre.x() - 10, centre.y() - 10, 20, 20), Qt::AlignCenter, labels[i]);
        }
        // The small buttons in the middle.
        p.setPen(Qt::NoPen);
        p.setBrush(body.lighter(160));
        p.drawRoundedRect(QRectF(128, 70, 14, 7), 3, 3);
        p.drawRoundedRect(QRectF(158, 70, 14, 7), 3, 3);
    }

private:
    Layout layout = Layout::Xbox;
};

std::vector<Common::ParamPackage> ControllersDialog::ConnectedPads(
    InputCommon::InputSubsystem* input) {
    std::vector<Common::ParamPackage> pads;
    if (input == nullptr) {
        return pads;
    }
    for (const auto& device : input->GetInputDevices()) {
        // SDL pads and Switch 2 controllers over Bluetooth; the extra "dual Joy-Con" entry
        // (guid2) is a pair already listed.
        const std::string engine = device.Get("engine", "");
        if ((engine == "sdl" || engine == "switch2") && device.Get("guid2", "").empty()) {
            pads.push_back(device);
        }
    }
    return pads;
}

ControllersDialog::ControllersDialog(InputCommon::InputSubsystem* input_,
                                     std::function<void()> configure, QWidget* parent)
    : QDialog(parent), input{input_} {
    setWindowTitle(tr("Controllers"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(720, 440);

    auto* const root = new QVBoxLayout(this);
    auto* const top = new QHBoxLayout;
    player = new QComboBox;
    for (int i = 0; i < 8; ++i) {
        player->addItem(tr("Player %1").arg(i + 1), i);
    }
    layout = new QComboBox;
    layout->addItem(tr("Automatic"), 0);
    layout->addItem(tr("Xbox layout"), static_cast<int>(Layout::Xbox));
    layout->addItem(tr("PlayStation layout"), static_cast<int>(Layout::PlayStation));
    layout->addItem(tr("Nintendo layout"), static_cast<int>(Layout::Nintendo));
    top->addWidget(player);
    top->addStretch(1);
    top->addWidget(new QLabel(tr("Picture")));
    top->addWidget(layout);
    root->addLayout(top);

    status = new QLabel;
    QFont status_font = status->font();
    status_font.setBold(true);
    status->setFont(status_font);
    root->addWidget(status);

    auto* const middle = new QHBoxLayout;
    auto* const left = new QVBoxLayout;
    art = new ControllerArt(this);
    left->addWidget(art, 1);
    left->addWidget(new QLabel(tr("Connected")));
    pads = new QListWidget;
    pads->setMaximumHeight(110);
    pads->setSelectionMode(QAbstractItemView::NoSelection);
    left->addWidget(pads);
    middle->addLayout(left, 3);

    auto* const right = new QGridLayout;
    right->addWidget(new QLabel(tr("Switch button")), 0, 0);
    right->addWidget(new QLabel(tr("Mapped to")), 0, 1);
    static constexpr std::array<const char*, 12> names{"A", "B",  "X",  "Y",    "L",    "R",
                                                       "ZL", "ZR", "+", "−", "L3", "R3"};
    for (size_t i = 0; i < mappings.size(); ++i) {
        auto* const name = new QLabel(QString::fromUtf8(names[i]));
        QFont font = name->font();
        font.setBold(true);
        name->setFont(font);
        mappings[i] = new QLabel;
        right->addWidget(name, static_cast<int>(i) + 1, 0);
        right->addWidget(mappings[i], static_cast<int>(i) + 1, 1);
    }
    right->setColumnStretch(1, 1);
    right->setRowStretch(static_cast<int>(mappings.size()) + 1, 1);
    middle->addLayout(right, 2);
    root->addLayout(middle, 1);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton* const configure_button =
        buttons->addButton(tr("Configure Controls..."), QDialogButtonBox::ActionRole);
    connect(configure_button, &QPushButton::clicked, this, [this, configure] {
        accept();
        if (configure) {
            configure();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(player, &QComboBox::currentIndexChanged, this, &ControllersDialog::Refresh);
    connect(layout, &QComboBox::currentIndexChanged, this, &ControllersDialog::Refresh);
    // Pads come and go, and pressing one makes it the one in use.
    timer.setInterval(500);
    connect(&timer, &QTimer::timeout, this, &ControllersDialog::Refresh);
    timer.start();
    Refresh();
}

void ControllersDialog::Refresh() {
    const std::vector<Common::ParamPackage> connected = ConnectedPads(input);
    const Common::ParamPackage* active = nullptr;
    u64 newest = 0;
    for (const auto& pad : connected) {
        const u64 activity = std::stoull(pad.Get("last_active", "0"));
        if (active == nullptr || activity > newest) {
            active = &pad;
            newest = activity;
        }
    }

    pads->clear();
    for (const auto& pad : connected) {
        const QString name = QString::fromStdString(pad.Get("display", "Controller"));
        auto* const item = new QListWidgetItem(&pad == active && connected.size() > 1
                                                   ? tr("%1  (in use)").arg(name)
                                                   : name);
        if (&pad == active) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        pads->addItem(item);
    }
    if (connected.empty()) {
        pads->addItem(tr("No controller connected (the keyboard still plays)"));
    }

    const QString active_name =
        active != nullptr ? QString::fromStdString(active->Get("display", "Controller")) : QString{};
    const int chosen = layout->currentData().toInt();
    art->SetLayout(chosen != 0 ? static_cast<Layout>(chosen) : LayoutFor(active_name));
    status->setText(connected.empty()
                        ? tr("No controller connected")
                        : (connected.size() == 1 ? tr("1 controller connected: %1")
                                                 : tr("%1 controllers connected, in use: %2")
                                                       .arg(connected.size()))
                              .arg(active_name));

    static constexpr std::array<int, 12> buttons{
        Settings::NativeButton::A,     Settings::NativeButton::B,     Settings::NativeButton::X,
        Settings::NativeButton::Y,     Settings::NativeButton::L,     Settings::NativeButton::R,
        Settings::NativeButton::ZL,    Settings::NativeButton::ZR,    Settings::NativeButton::Plus,
        Settings::NativeButton::Minus, Settings::NativeButton::LStick, Settings::NativeButton::RStick};
    const auto& bindings =
        Settings::values.players.GetValue()[static_cast<size_t>(player->currentData().toInt())]
            .buttons;
    for (size_t i = 0; i < mappings.size(); ++i) {
        mappings[i]->setText(
            InputBindingText(input, Common::ParamPackage{bindings[static_cast<size_t>(buttons[i])]}));
    }
}
