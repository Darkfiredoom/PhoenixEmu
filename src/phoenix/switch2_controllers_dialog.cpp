// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "input_common/drivers/switch2.h"
#include "input_common/main.h"
#include "qt_common/config/uisettings.h"
#include "phoenix/switch2_controllers_dialog.h"

namespace {

QLabel* Step(int number, const QString& text, QWidget* parent) {
    auto* const label = new QLabel(
        QStringLiteral("<span style='color:#FF8A3D; font-weight:800;'>%1</span>&nbsp;&nbsp;%2")
            .arg(number)
            .arg(text),
        parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    return label;
}

} // Anonymous namespace

Switch2ControllersDialog::Switch2ControllersDialog(InputCommon::InputSubsystem* input_subsystem_,
                                                   QWidget* parent)
    : QDialog(parent), input_subsystem{input_subsystem_} {
    setWindowTitle(tr("Switch 2 Controllers"));
    setMinimumWidth(560);
    setObjectName(QStringLiteral("Switch2ControllersDialog"));
    setStyleSheet(QStringLiteral(R"(
        QLabel#S2Title { font-size: 20pt; font-weight: 800; color: #FFF4DA; }
        QLabel#S2Subtitle { color: rgba(233,220,194,170); }
        QFrame#S2Card { background: rgba(255,230,180,10); border: 1px solid rgba(255,200,61,30);
            border-radius: 14px; }
        QLabel#S2Section { font-weight: 800; color: rgba(233,220,194,160); }
        QLabel#S2Name { font-weight: 800; color: #FFF4DA; font-size: 11pt; }
        QLabel#S2Meta { color: rgba(233,220,194,160); }
        QLabel#S2Badge { background: rgba(255,82,48,50); border: 1px solid #FF8A3D; border-radius: 9px;
            color: #FFB08A; font-weight: 800; padding: 1px 9px; }
        QLabel#S2Problem { color: #FF8A6A; font-weight: 700; }
        QLabel#S2Credit { color: rgba(233,220,194,110); font-size: 8.5pt; }
    )"));

    auto* const root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 16);
    root->setSpacing(12);

    auto* const title = new QLabel(tr("Switch 2 controllers"), this);
    title->setObjectName(QStringLiteral("S2Title"));
    auto* const subtitle =
        new QLabel(tr("Joy-Con 2, Pro Controller 2 and the GameCube controller, over Bluetooth. "
                      "No pairing in Windows' Bluetooth settings needed."),
                   this);
    subtitle->setObjectName(QStringLiteral("S2Subtitle"));
    subtitle->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(subtitle);

    // How to: sync once, then a button press reconnects it.
    auto* const steps = new QFrame(this);
    steps->setObjectName(QStringLiteral("S2Card"));
    auto* const steps_column = new QVBoxLayout(steps);
    steps_column->setContentsMargins(16, 14, 16, 14);
    steps_column->setSpacing(8);
    auto* const steps_title = new QLabel(tr("Pair a controller"), steps);
    steps_title->setObjectName(QStringLiteral("S2Section"));
    steps_column->addWidget(steps_title);
    steps_column->addWidget(Step(
        1, tr("Hold the small sync button on the controller until its lights start to run."),
        steps));
    steps_column->addWidget(
        Step(2, tr("Wait a moment: it pairs to this PC and its player light comes on."), steps));
    steps_column->addWidget(Step(
        3, tr("Next time, just press any button on it to reconnect (PhoenixEmu must be open)."),
        steps));
    steps_column->addWidget(Step(
        4, tr("It becomes Player 1 by itself while Player 1 is on the keyboard (a left and a "
              "right Joy-Con 2 together, as a pair). Otherwise pick it in Settings → "
              "Controls."),
        steps));
    auto* const note = new QLabel(
        tr("Paired it in Windows' Bluetooth settings before? Remove it there first."), steps);
    note->setObjectName(QStringLiteral("S2Meta"));
    note->setWordWrap(true);
    steps_column->addWidget(note);
    root->addWidget(steps);

    // Mouse mode: a Joy-Con 2 on its side on a table moves the pointer.
    auto* const mouse = new QCheckBox(
        tr("Use a Joy-Con 2 as a mouse when it lies on its side on a table (L/R click, ZL/ZR "
           "right-click, the stick scrolls)"),
        this);
    mouse->setChecked(UISettings::values.switch2_mouse.GetValue());
    connect(mouse, &QCheckBox::toggled, this, [this](bool on) {
        UISettings::values.switch2_mouse = on;
        if (input_subsystem != nullptr) {
            if (auto* const switch2 = input_subsystem->GetSwitch2Controllers()) {
                switch2->SetMouseEnabled(on);
            }
        }
    });
    root->addWidget(mouse);

    problem = new QLabel(this);
    problem->setObjectName(QStringLiteral("S2Problem"));
    problem->setWordWrap(true);
    problem->hide();
    root->addWidget(problem);

    // The controllers connected now.
    auto* const connected = new QLabel(tr("Connected"), this);
    connected->setObjectName(QStringLiteral("S2Section"));
    root->addWidget(connected);
    auto* const list_frame = new QFrame(this);
    list_frame->setObjectName(QStringLiteral("S2Card"));
    list = new QVBoxLayout(list_frame);
    list->setContentsMargins(14, 10, 14, 10);
    list->setSpacing(8);
    empty = new QLabel(tr("No Switch 2 controller yet: looking for one…"), list_frame);
    empty->setObjectName(QStringLiteral("S2Meta"));
    list->addWidget(empty);
    root->addWidget(list_frame);

    auto* const credit = new QLabel(
        tr("The controllers' Bluetooth protocol is as Nadeflore's switch2-controllers documents "
           "it."),
        this);
    credit->setObjectName(QStringLiteral("S2Credit"));
    credit->setWordWrap(true);
    root->addWidget(credit);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &Switch2ControllersDialog::Refresh);
    timer->start();
    Refresh();
}

void Switch2ControllersDialog::Refresh() {
    using InputCommon::Switch2Controllers;
    Switch2Controllers* const engine =
        input_subsystem != nullptr ? input_subsystem->GetSwitch2Controllers() : nullptr;
    const QString why = engine == nullptr
                            ? tr("Switch 2 controllers can be paired from PhoenixEmu on Windows "
                                 "only for now.")
                            : QString::fromStdString(engine->BluetoothProblem());
    problem->setText(why);
    problem->setVisible(!why.isEmpty());
    const auto status = engine != nullptr ? engine->GetStatus()
                                          : std::vector<Switch2Controllers::Status>{};

    // Rebuilt only when something changed, so the list doesn't flicker.
    QString shown;
    for (const auto& controller : status) {
        shown += QStringLiteral("%1/%2/%3/%4;")
                     .arg(static_cast<int>(controller.kind))
                     .arg(controller.port)
                     .arg(controller.battery_percent / 5)
                     .arg(controller.paired_now);
    }
    if (shown == last_shown) {
        return;
    }
    last_shown = shown;
    while (list->count() > 1) {
        QLayoutItem* const item = list->takeAt(1);
        if (QWidget* const widget = item->widget()) {
            widget->deleteLater();
        } else if (QLayout* const layout = item->layout()) {
            while (QLayoutItem* const child = layout->takeAt(0)) {
                if (child->widget() != nullptr) {
                    child->widget()->deleteLater();
                }
                delete child;
            }
        }
        delete item;
    }
    empty->setVisible(status.empty());
    for (const auto& controller : status) {
        auto* const row = new QWidget(list->parentWidget());
        auto* const row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        auto* const text = new QVBoxLayout;
        text->setSpacing(0);
        auto* const name = new QLabel(
            QString::fromStdString(Switch2Controllers::KindName(controller.kind)), row);
        name->setObjectName(QStringLiteral("S2Name"));
        auto* const meta = new QLabel(
            controller.battery_percent >= 0
                ? tr("Player light %1 · battery %2%")
                      .arg(controller.port + 1)
                      .arg(controller.battery_percent)
                : tr("Player light %1").arg(controller.port + 1),
            row);
        meta->setObjectName(QStringLiteral("S2Meta"));
        text->addWidget(name);
        text->addWidget(meta);
        row_layout->addLayout(text, 1);
        if (controller.paired_now) {
            auto* const badge = new QLabel(tr("Paired just now"), row);
            badge->setObjectName(QStringLiteral("S2Badge"));
            row_layout->addWidget(badge);
        }
        list->addWidget(row);
    }
}
