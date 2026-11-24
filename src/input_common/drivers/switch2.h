// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "input_common/input_engine.h"

namespace InputCommon {

/**
 * Switch 2 controllers (Joy-Con 2, Pro Controller 2, the NSO GameCube controller) over
 * Bluetooth LE, without pairing them in Windows' Bluetooth settings: a controller held in sync
 * mode is paired to this PC, one already paired reconnects when a button is pressed. Buttons,
 * sticks (with the controller's own calibration), motion, battery and rumble come through as for
 * any controller; a left and a right Joy-Con 2 are also listed as one pair.
 *
 * The protocol (the GATT characteristics, the command format, the input report's layout) is as
 * Nadeflore's switch2-controllers documents it (github.com/Nadeflore/switch2-controllers).
 * Windows only: elsewhere the engine lists no controllers.
 */
class Switch2Controllers final : public InputEngine {
public:
    enum class Kind : u8 { None = 0, JoyconLeft, JoyconRight, Pro, GameCube, Dual };

    /// A controller as the pairing window shows it.
    struct Status {
        Kind kind{};
        std::size_t port{};
        std::string address;
        int battery_percent{-1};
        bool paired_now{}; ///< Paired to this PC in this session (it was in sync mode)
    };

    explicit Switch2Controllers(std::string input_engine_);
    ~Switch2Controllers() override;

    std::vector<Common::ParamPackage> GetInputDevices() const override;
    ButtonMapping GetButtonMappingForDevice(const Common::ParamPackage& params) override;
    AnalogMapping GetAnalogMappingForDevice(const Common::ParamPackage& params) override;
    MotionMapping GetMotionMappingForDevice(const Common::ParamPackage& params) override;
    Common::Input::ButtonNames GetUIName(const Common::ParamPackage& params) const override;
    Common::Input::DriverResult SetVibration(
        const PadIdentifier& identifier, const Common::Input::VibrationStatus& vibration) override;
    bool IsVibrationEnabled(const PadIdentifier& identifier) override;

    /// The controllers connected now.
    [[nodiscard]] std::vector<Status> GetStatus() const;

    /// Empty when Bluetooth LE can be used here, else why not (for the pairing window).
    [[nodiscard]] std::string BluetoothProblem() const;

    [[nodiscard]] static std::string KindName(Kind kind);

    /// A Joy-Con 2 on its side on a table moves the pointer (its optical sensor): L/R click,
    /// ZL/ZR right-click, the stick scrolls and its click is the middle button. On by default.
    void SetMouseEnabled(bool enabled);
    [[nodiscard]] bool IsMouseEnabled() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

} // namespace InputCommon
