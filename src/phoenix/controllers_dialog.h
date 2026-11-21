// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <vector>

#include <QDialog>
#include <QTimer>

#include "common/param_package.h"

class QComboBox;
class QLabel;
class QListWidget;

namespace InputCommon {
class InputSubsystem;
}

/// The gamepads PhoenixEmu sees right now (the one last pressed is the one "in use"), and what
/// each Switch button is mapped to on it for a player. After the controller page of Jinra's Home.
class ControllersDialog : public QDialog {
    Q_OBJECT

public:
    /// The connected gamepads: SDL controllers, one entry per pad (a Joy-Con pair counts once).
    static std::vector<Common::ParamPackage> ConnectedPads(InputCommon::InputSubsystem* input);

    /// `configure` opens Configure on its Controls page.
    ControllersDialog(InputCommon::InputSubsystem* input, std::function<void()> configure,
                      QWidget* parent = nullptr);

private:
    void Refresh();

    InputCommon::InputSubsystem* input;
    QListWidget* pads{};
    QComboBox* player{};
    QComboBox* layout{};
    class ControllerArt* art{};
    QLabel* status{};
    std::array<QLabel*, 12> mappings{};
    QTimer timer;
};
