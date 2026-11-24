// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QLabel;
class QTimer;
class QVBoxLayout;

namespace InputCommon {
class InputSubsystem;
}

/// Pairs Switch 2 controllers (Joy-Con 2, Pro Controller 2, the NSO GameCube controller) over
/// Bluetooth LE and shows the ones connected, with their player number and battery
/// (input_common/drivers/switch2.cpp does the work; this window explains it and watches).
class Switch2ControllersDialog : public QDialog {
    Q_OBJECT

public:
    explicit Switch2ControllersDialog(InputCommon::InputSubsystem* input_subsystem,
                                      QWidget* parent = nullptr);

private:
    void Refresh();

    InputCommon::InputSubsystem* input_subsystem;
    QLabel* problem{};
    QLabel* empty{};
    QVBoxLayout* list{};
    QTimer* timer{};
    QString last_shown;
};
