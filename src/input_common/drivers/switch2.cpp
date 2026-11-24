// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include <fmt/format.h>

#include "common/logging.h"
#include "common/param_package.h"
#include "common/settings_input.h"
#include "input_common/drivers/switch2.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace InputCommon {

namespace {

using Kind = Switch2Controllers::Kind;

constexpr std::size_t MaxPorts = 8;

// Identification: Nintendo's Bluetooth company ID, its USB vendor ID, the controllers' product IDs.
constexpr u16 NintendoCompanyId = 0x0553;
constexpr u16 NintendoVendorId = 0x057E;
constexpr u16 JoyconRightPid = 0x2066;
constexpr u16 JoyconLeftPid = 0x2067;
constexpr u16 ProControllerPid = 0x2069;
constexpr u16 GameCubePid = 0x2073;

Kind KindFromPid(u16 pid) {
    switch (pid) {
    case JoyconLeftPid:
        return Kind::JoyconLeft;
    case JoyconRightPid:
        return Kind::JoyconRight;
    case ProControllerPid:
        return Kind::Pro;
    case GameCubePid:
        return Kind::GameCube;
    default:
        return Kind::None;
    }
}

/// The input report's button bits, used as the engine's button ids.
enum Button : int {
    Y = 0,
    X = 1,
    B = 2,
    A = 3,
    SrRight = 4,
    SlRight = 5,
    R = 6,
    ZR = 7,
    Minus = 8,
    Plus = 9,
    StickR = 10,
    StickL = 11,
    Home = 12,
    Capture = 13,
    C = 14,
    Down = 16,
    Up = 17,
    Right = 18,
    Left = 19,
    SrLeft = 20,
    SlLeft = 21,
    L = 22,
    ZL = 23,
    GripRight = 24,
    GripLeft = 25,
};
constexpr int ButtonCount = 26;

enum Axis : int { LeftX = 0, LeftY = 1, RightX = 2, RightY = 3 };

PadIdentifier IdentifierOf(std::size_t port, Kind kind) {
    // A GUID of its own (0x5E: "Switch 2"), so these never collide with the Joy-Con engine's.
    const std::array<u8, 16> guid{0x5E, 0x02, 0, 0, 0, 0, 0, 0,
                                  0,    0,    0, 0, 0, 0, 0, static_cast<u8>(kind)};
    return {.guid = Common::UUID{guid}, .port = port, .pad = static_cast<std::size_t>(kind)};
}

/// A stick's calibration (from the controller's memory): centre, and the reach either way.
struct StickCalibration {
    int center_x{2048}, center_y{2048};
    int max_x{1400}, max_y{1400};
    int min_x{1400}, min_y{1400};

    static std::pair<int, int> XY(const u8* bytes) {
        const u32 value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
        return {static_cast<int>(value & 0xFFF), static_cast<int>(value >> 12)};
    }

    void Load(const std::vector<u8>& data) {
        if (data.size() < 9) {
            return;
        }
        std::tie(center_x, center_y) = XY(&data[0]);
        std::tie(max_x, max_y) = XY(&data[3]);
        std::tie(min_x, min_y) = XY(&data[6]);
        // A blank calibration would divide by zero: keep sane reaches.
        max_x = std::max(max_x, 200);
        max_y = std::max(max_y, 200);
        min_x = std::max(min_x, 200);
        min_y = std::max(min_y, 200);
    }

    static float Axis(int raw, int center, int reach_up, int reach_down) {
        constexpr int DeadZone = 40;
        const int value = raw - center;
        if (value > DeadZone) {
            return std::min(static_cast<float>(value) / static_cast<float>(reach_up), 1.0f);
        }
        if (value < -DeadZone) {
            return -std::min(static_cast<float>(-value) / static_cast<float>(reach_down), 1.0f);
        }
        return 0.0f;
    }

    std::pair<float, float> Apply(const u8* bytes) const {
        const auto [x, y] = XY(bytes);
        return {Axis(x, center_x, max_x, min_x), Axis(y, center_y, max_y, min_y)};
    }
};

u16 ReadU16(const u8* bytes) {
    return static_cast<u16>(bytes[0] | (bytes[1] << 8));
}

s16 ReadS16(const u8* bytes) {
    return static_cast<s16>(ReadU16(bytes));
}

} // Anonymous namespace

std::string Switch2Controllers::KindName(Kind kind) {
    switch (kind) {
    case Kind::JoyconLeft:
        return "Joy-Con 2 (L)";
    case Kind::JoyconRight:
        return "Joy-Con 2 (R)";
    case Kind::Pro:
        return "Pro Controller 2";
    case Kind::GameCube:
        return "GameCube Controller (NSO)";
    case Kind::Dual:
        return "Joy-Con 2 pair";
    case Kind::None:
        break;
    }
    return "Switch 2 controller";
}

#ifdef _WIN32

namespace {

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;

// The GATT characteristics.
constexpr winrt::guid InputReportUuid{
    0xab7de9be, 0x89fe, 0x49ad, {0x82, 0x8f, 0x11, 0x8f, 0x09, 0xdf, 0x7f, 0xd2}};
constexpr winrt::guid CommandWriteUuid{
    0x649d4ac9, 0x8eb7, 0x4e6c, {0xaf, 0x44, 0x1e, 0xa5, 0x4f, 0xe5, 0xf0, 0x05}};
constexpr winrt::guid CommandResponseUuid{
    0xc765a961, 0xd9d8, 0x4d36, {0xa2, 0x0a, 0x53, 0x15, 0xb1, 0x11, 0x83, 0x6a}};
constexpr winrt::guid VibrationRightUuid{
    0xfa19b0fb, 0xcd1f, 0x46a7, {0x84, 0xa1, 0xbb, 0xb0, 0x9e, 0x00, 0xc1, 0x49}};
constexpr winrt::guid VibrationLeftUuid{
    0x289326cb, 0xa471, 0x485d, {0xa8, 0xf4, 0x24, 0x0c, 0x14, 0xf1, 0x82, 0x41}};
constexpr winrt::guid VibrationProUuid{
    0xcc483f51, 0x9258, 0x427d, {0xa9, 0x39, 0x63, 0x0c, 0x31, 0xf7, 0x2b, 0x05}};

// Commands: id, then sub-command.
constexpr u8 CommandMemory = 0x02, SubMemoryRead = 0x04;
constexpr u8 CommandLeds = 0x09, SubLedsSetPlayer = 0x07;
constexpr u8 CommandFeature = 0x0c, SubFeatureInit = 0x02, SubFeatureEnable = 0x04;
constexpr u8 CommandPair = 0x15, SubPairSetAddress = 0x01, SubPairKey2 = 0x02,
             SubPairFinish = 0x03, SubPairKey1 = 0x04;
constexpr u8 FeatureMotion = 0x04;
constexpr u8 FeatureMouse = 0x10;

// Memory: the controller's description, and its sticks' calibrations (the player's own first).
constexpr u32 AddressControllerInfo = 0x00013000;
constexpr u32 AddressStick1 = 0x0130A8, AddressStick2 = 0x0130E8;
constexpr u32 AddressUserStick1 = 0x1fc042, AddressUserStick2 = 0x1fc062;

// The player lights for players 1 to 8, as a Switch shows them.
constexpr std::array<u8, 8> PlayerLeds{0x01, 0x03, 0x07, 0x0F, 0x09, 0x05, 0x0D, 0x06};

std::vector<u8> BytesOf(const IBuffer& buffer) {
    std::vector<u8> bytes(buffer.Length());
    if (!bytes.empty()) {
        DataReader::FromBuffer(buffer).ReadBytes(bytes);
    }
    return bytes;
}

IBuffer BufferOf(const std::vector<u8>& bytes) {
    DataWriter writer;
    writer.WriteBytes(bytes);
    return writer.DetachBuffer();
}

std::string AddressText(u64 address) {
    return fmt::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", (address >> 40) & 0xFF,
                       (address >> 32) & 0xFF, (address >> 24) & 0xFF, (address >> 16) & 0xFF,
                       (address >> 8) & 0xFF, address & 0xFF);
}

} // Anonymous namespace

struct Switch2Controllers::Impl {
    struct Controller {
        BluetoothLEDevice device{nullptr};
        GattCharacteristic input{nullptr};
        GattCharacteristic command{nullptr};
        GattCharacteristic response{nullptr};
        GattCharacteristic vibration{nullptr};
        Kind kind{Kind::None};
        std::size_t port{};
        u64 address{};
        bool paired_now{};
        StickCalibration left_stick;
        StickCalibration right_stick;
        std::atomic<int> battery{-1};
        std::atomic<bool> alive{true};
        u8 vibration_id{};
        std::mutex command_mutex;
        std::mutex reply_mutex;
        std::condition_variable reply_ready;
        std::optional<std::vector<u8>> reply;
        u32 previous_buttons{};
        // Mouse mode (a Joy-Con on its side on a table).
        bool mouse_active{};
        u16 mouse_x{};
        u16 mouse_y{};
        u8 mouse_buttons{}; ///< Bits: left, right, middle, as last sent
    };

    std::atomic<bool> mouse_enabled{true};

    explicit Impl(Switch2Controllers& engine_) : engine{engine_} {
        worker = std::jthread([this](std::stop_token stop) { Run(stop); });
    }

    ~Impl() {
        worker.request_stop();
        queue_ready.notify_all();
    }

    Switch2Controllers& engine;
    std::jthread worker;

    mutable std::mutex mutex; ///< Guards the slots, the queue and the problem text
    std::array<std::array<std::shared_ptr<Controller>, MaxPorts>, 5> slots{}; // by Kind
    std::deque<std::pair<u64, std::pair<u16, bool>>> queue; ///< address, (pid, pairing)
    std::vector<u64> busy; ///< Addresses connected or being connected
    std::condition_variable queue_ready;
    std::string problem;
    u64 host_address{};

    void SetProblem(std::string text) {
        std::scoped_lock lock{mutex};
        problem = std::move(text);
    }

    void Run(std::stop_token stop) {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error&) {
            // Already initialised on this thread: fine.
        }
        // Bluetooth may be off, or turned on later: try again every few seconds until it works.
        BluetoothLEAdvertisementWatcher watcher{nullptr};
        while (!stop.stop_requested() && watcher == nullptr) {
            try {
                const BluetoothAdapter adapter = BluetoothAdapter::GetDefaultAsync().get();
                if (adapter == nullptr || !adapter.IsLowEnergySupported()) {
                    SetProblem("This PC has no Bluetooth LE adapter.");
                    return;
                }
                host_address = adapter.BluetoothAddress();
                BluetoothLEAdvertisementWatcher candidate;
                candidate.ScanningMode(BluetoothLEScanningMode::Active);
                candidate.Received([this](const BluetoothLEAdvertisementWatcher&,
                                          const BluetoothLEAdvertisementReceivedEventArgs& args) {
                    OnAdvertisement(args);
                });
                candidate.Start();
                watcher = candidate;
                SetProblem({});
                LOG_INFO(Input, "Switch 2 controllers: looking for controllers over Bluetooth LE");
            } catch (const winrt::hresult_error& error) {
                // 0x80070015 (not ready): the radio is off.
                SetProblem(error.code() == winrt::hresult{static_cast<int32_t>(0x80070015)}
                               ? "Bluetooth is turned off. Turn it on in Windows' settings "
                                 "(Bluetooth & devices): PhoenixEmu finds your controllers then."
                               : fmt::format("Bluetooth could not be used ({}).",
                                             winrt::to_string(error.message())));
                std::unique_lock lock{mutex};
                queue_ready.wait_for(lock, std::chrono::seconds(5),
                                     [&] { return stop.stop_requested(); });
            }
        }
        if (watcher == nullptr) {
            return;
        }

        while (!stop.stop_requested()) {
            std::pair<u64, std::pair<u16, bool>> next{};
            {
                std::unique_lock lock{mutex};
                queue_ready.wait_for(lock, std::chrono::milliseconds(500),
                                     [&] { return stop.stop_requested() || !queue.empty(); });
                if (stop.stop_requested()) {
                    break;
                }
                if (queue.empty()) {
                    continue;
                }
                next = queue.front();
                queue.pop_front();
            }
            const auto [address, info] = next;
            if (!Connect(address, info.first, info.second)) {
                std::scoped_lock lock{mutex};
                std::erase(busy, address);
            }
        }

        try {
            watcher.Stop();
        } catch (const winrt::hresult_error&) {
        }
        std::scoped_lock lock{mutex};
        for (auto& kind_slots : slots) {
            for (auto& controller : kind_slots) {
                if (controller != nullptr) {
                    controller->alive = false;
                    try {
                        controller->device.Close();
                    } catch (const winrt::hresult_error&) {
                    }
                    controller.reset();
                }
            }
        }
    }

    void OnAdvertisement(const BluetoothLEAdvertisementReceivedEventArgs& args) {
        for (const auto& section : args.Advertisement().ManufacturerData()) {
            if (section.CompanyId() != NintendoCompanyId) {
                continue;
            }
            const std::vector<u8> data = BytesOf(section.Data());
            if (data.size() < 16) {
                continue;
            }
            const u16 vendor = ReadU16(&data[3]);
            const u16 product = ReadU16(&data[5]);
            if (vendor != NintendoVendorId || KindFromPid(product) == Kind::None) {
                continue;
            }
            // The address the controller would reconnect to: none while in sync mode.
            u64 reconnect = 0;
            for (int i = 5; i >= 0; --i) {
                reconnect = (reconnect << 8) | data[10 + i];
            }
            const bool pairing = reconnect == 0;
            if (!pairing && reconnect != host_address) {
                continue; // paired to another console or PC
            }
            const u64 address = args.BluetoothAddress();
            std::scoped_lock lock{mutex};
            if (std::ranges::find(busy, address) != busy.end()) {
                continue;
            }
            busy.push_back(address);
            queue.push_back({address, {product, pairing}});
            queue_ready.notify_all();
        }
    }

    /// Sends a command and waits for its reply's payload (empty on failure).
    std::optional<std::vector<u8>> Command(Controller& c, u8 command, u8 sub,
                                           const std::vector<u8>& data) {
        std::scoped_lock command_lock{c.command_mutex};
        std::vector<u8> packet{command, 0x91, 0x01, sub, 0x00, static_cast<u8>(data.size()),
                               0x00, 0x00};
        packet.insert(packet.end(), data.begin(), data.end());
        {
            std::scoped_lock lock{c.reply_mutex};
            c.reply.reset();
        }
        const auto option = (c.command.CharacteristicProperties() &
                             GattCharacteristicProperties::WriteWithoutResponse) !=
                                    GattCharacteristicProperties::None
                                ? GattWriteOption::WriteWithoutResponse
                                : GattWriteOption::WriteWithResponse;
        if (c.command.WriteValueAsync(BufferOf(packet), option).get() !=
            GattCommunicationStatus::Success) {
            return std::nullopt;
        }
        std::unique_lock lock{c.reply_mutex};
        if (!c.reply_ready.wait_for(lock, std::chrono::seconds(2),
                                    [&] { return c.reply.has_value(); })) {
            return std::nullopt;
        }
        const std::vector<u8>& reply = *c.reply;
        if (reply.size() < 8 || reply[0] != command || reply[1] != 0x01) {
            return std::nullopt;
        }
        return std::vector<u8>(reply.begin() + 8, reply.end());
    }

    std::optional<std::vector<u8>> ReadMemory(Controller& c, u8 length, u32 address) {
        const std::vector<u8> request{length,
                                      0x7e,
                                      0x00,
                                      0x00,
                                      static_cast<u8>(address),
                                      static_cast<u8>(address >> 8),
                                      static_cast<u8>(address >> 16),
                                      static_cast<u8>(address >> 24)};
        auto data = Command(c, CommandMemory, SubMemoryRead, request);
        if (!data || data->size() < 8 || (*data)[0] != length) {
            return std::nullopt;
        }
        return std::vector<u8>(data->begin() + 8, data->end());
    }

    StickCalibration ReadStick(Controller& c, u32 user_address, u32 factory_address) {
        StickCalibration calibration;
        auto data = ReadMemory(c, 0x0b, user_address);
        if (!data || data->size() < 3 || ((*data)[0] == 0xFF && (*data)[1] == 0xFF &&
                                          (*data)[2] == 0xFF)) {
            data = ReadMemory(c, 0x0b, factory_address);
        }
        if (data) {
            calibration.Load(*data);
        }
        return calibration;
    }

    bool Pair(Controller& c) {
        // This PC's address twice (a Switch 2 sends both of its adapters'), then the keys.
        std::vector<u8> address{0x00, 0x02};
        for (int round = 0; round < 2; ++round) {
            for (int i = 0; i < 6; ++i) {
                address.push_back(static_cast<u8>(host_address >> (8 * i)));
            }
        }
        const std::vector<u8> key1{0x00, 0xea, 0xbd, 0x47, 0x13, 0x89, 0x35, 0x42, 0xc6,
                                   0x79, 0xee, 0x07, 0xf2, 0x53, 0x2c, 0x6c, 0x31};
        const std::vector<u8> key2{0x00, 0x40, 0xb0, 0x8a, 0x5f, 0xcd, 0x1f, 0x9b, 0x41,
                                   0x12, 0x5c, 0xac, 0xc6, 0x3f, 0x38, 0xa0, 0x73};
        return Command(c, CommandPair, SubPairSetAddress, address) &&
               Command(c, CommandPair, SubPairKey1, key1) &&
               Command(c, CommandPair, SubPairKey2, key2) &&
               Command(c, CommandPair, SubPairFinish, {0x00});
    }

    bool Connect(u64 address, u16 product, bool pairing) {
        auto c = std::make_shared<Controller>();
        c->address = address;
        c->kind = KindFromPid(product);
        c->paired_now = pairing;
        try {
            c->device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
            if (c->device == nullptr) {
                return false;
            }
            try {
                // A shorter connection interval: input every few milliseconds, not every 30.
                c->device.RequestPreferredConnectionParameters(
                    BluetoothLEPreferredConnectionParameters::ThroughputOptimized());
            } catch (const winrt::hresult_error&) {
                // Older Windows: the default interval.
            }
            const auto services = c->device.GetGattServicesAsync(BluetoothCacheMode::Uncached).get();
            if (services.Status() != GattCommunicationStatus::Success) {
                return false;
            }
            for (const auto& service : services.Services()) {
                const auto characteristics =
                    service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached).get();
                if (characteristics.Status() != GattCommunicationStatus::Success) {
                    continue;
                }
                for (const auto& characteristic : characteristics.Characteristics()) {
                    const winrt::guid uuid = characteristic.Uuid();
                    if (uuid == InputReportUuid) {
                        c->input = characteristic;
                    } else if (uuid == CommandWriteUuid) {
                        c->command = characteristic;
                    } else if (uuid == CommandResponseUuid) {
                        c->response = characteristic;
                    } else if ((uuid == VibrationLeftUuid && c->kind == Kind::JoyconLeft) ||
                               (uuid == VibrationRightUuid && c->kind == Kind::JoyconRight) ||
                               (uuid == VibrationProUuid &&
                                (c->kind == Kind::Pro || c->kind == Kind::GameCube))) {
                        c->vibration = characteristic;
                    }
                }
            }
            if (c->input == nullptr || c->command == nullptr || c->response == nullptr) {
                LOG_WARNING(Input, "Switch 2 controllers: {} lacks the expected services",
                            AddressText(address));
                return false;
            }
            std::weak_ptr<Controller> weak = c;
            c->response.ValueChanged([weak](const GattCharacteristic&,
                                            const GattValueChangedEventArgs& args) {
                if (const auto controller = weak.lock()) {
                    std::scoped_lock lock{controller->reply_mutex};
                    controller->reply = BytesOf(args.CharacteristicValue());
                    controller->reply_ready.notify_all();
                }
            });
            if (c->response
                    .WriteClientCharacteristicConfigurationDescriptorAsync(
                        GattClientCharacteristicConfigurationDescriptorValue::Notify)
                    .get() != GattCommunicationStatus::Success) {
                return false;
            }

            // Its sticks' calibrations: a Joy-Con keeps its one stick's in the first slot.
            const StickCalibration first = ReadStick(*c, AddressUserStick1, AddressStick1);
            if (c->kind == Kind::JoyconRight) {
                c->right_stick = first;
            } else {
                c->left_stick = first;
                c->right_stick = ReadStick(*c, AddressUserStick2, AddressStick2);
            }
            if (pairing && !Pair(*c)) {
                LOG_WARNING(Input, "Switch 2 controllers: pairing {} failed", AddressText(address));
                return false;
            }

            // Its slot: the first free port of its kind.
            {
                std::scoped_lock lock{mutex};
                auto& kind_slots = slots[static_cast<std::size_t>(c->kind)];
                const auto free = std::ranges::find(kind_slots, nullptr);
                if (free == kind_slots.end()) {
                    return false;
                }
                c->port = static_cast<std::size_t>(free - kind_slots.begin());
                *free = c;
            }
            engine.PreSetController(IdentifierOf(c->port, c->kind));
            for (int button = 0; button < ButtonCount; ++button) {
                engine.PreSetButton(IdentifierOf(c->port, c->kind), button);
            }
            for (int axis = 0; axis < 4; ++axis) {
                engine.PreSetAxis(IdentifierOf(c->port, c->kind), axis);
            }
            engine.PreSetMotion(IdentifierOf(c->port, c->kind), 0);
            engine.PreSetMotion(IdentifierOf(c->port, c->kind), 1);

            // Its player light, motion, then its input.
            std::vector<u8> leds{PlayerLeds[std::min<std::size_t>(c->port, 7)], 0, 0, 0};
            (void)Command(*c, CommandLeds, SubLedsSetPlayer, leds);
            const bool joycon = c->kind == Kind::JoyconLeft || c->kind == Kind::JoyconRight;
            const u8 features = joycon ? u8(FeatureMotion | FeatureMouse) : FeatureMotion;
            (void)Command(*c, CommandFeature, SubFeatureInit, {features, 0, 0, 0});
            (void)Command(*c, CommandFeature, SubFeatureEnable, {features, 0, 0, 0});
            c->input.ValueChanged([this, weak](const GattCharacteristic&,
                                               const GattValueChangedEventArgs& args) {
                if (const auto controller = weak.lock(); controller && controller->alive) {
                    OnInput(*controller, BytesOf(args.CharacteristicValue()));
                }
            });
            if (c->input
                    .WriteClientCharacteristicConfigurationDescriptorAsync(
                        GattClientCharacteristicConfigurationDescriptorValue::Notify)
                    .get() != GattCommunicationStatus::Success) {
                Remove(c);
                return false;
            }
            c->device.ConnectionStatusChanged([this, weak](const BluetoothLEDevice& device,
                                                           const auto&) {
                if (device.ConnectionStatus() == BluetoothConnectionStatus::Disconnected) {
                    if (const auto controller = weak.lock()) {
                        Remove(controller);
                    }
                }
            });
            LOG_INFO(Input, "Switch 2 controllers: {} {} connected as port {}{}",
                     KindName(c->kind), AddressText(address), c->port + 1,
                     pairing ? " (paired now)" : "");
            return true;
        } catch (const winrt::hresult_error& error) {
            LOG_WARNING(Input, "Switch 2 controllers: {} could not be connected ({})",
                        AddressText(address), winrt::to_string(error.message()));
            Remove(c);
            return false;
        }
    }

    void Remove(const std::shared_ptr<Controller>& c) {
        c->alive = false;
        bool was_slotted = false;
        {
            std::scoped_lock lock{mutex};
            auto& slot = slots[static_cast<std::size_t>(c->kind)][c->port];
            if (slot == c) {
                slot.reset();
                was_slotted = true;
            }
            std::erase(busy, c->address);
        }
        if (was_slotted) {
            const auto identifier = IdentifierOf(c->port, c->kind);
            for (int button = 0; button < ButtonCount; ++button) {
                engine.SetButton(identifier, button, false);
            }
            for (int axis = 0; axis < 4; ++axis) {
                engine.SetAxis(identifier, axis, 0.0f);
            }
            engine.SetBattery(identifier, Common::Input::BatteryLevel::None);
            LOG_INFO(Input, "Switch 2 controllers: {} disconnected", KindName(c->kind));
        }
        try {
            if (c->device != nullptr) {
                c->device.Close();
            }
        } catch (const winrt::hresult_error&) {
        }
    }

    static void SendMouse(DWORD flags, LONG dx = 0, LONG dy = 0, DWORD data = 0) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.mouseData = data;
        input.mi.dwFlags = flags;
        SendInput(1, &input, sizeof(input));
    }

    /// A Joy-Con 2 lying on its side on a table: its optical sensor moves the pointer. Returns
    /// whether it is a mouse now; the buttons it uses are taken out of `buttons`.
    bool Mouse(Controller& c, const std::vector<u8>& data, u32& buttons) {
        const bool joycon = c.kind == Kind::JoyconLeft || c.kind == Kind::JoyconRight;
        const u16 x = ReadU16(&data[16]);
        const u16 y = ReadU16(&data[18]);
        const u16 roughness = ReadU16(&data[20]);
        const u16 distance = ReadU16(&data[22]);
        // On a surface: close (and not lifted off) and smooth enough to track.
        const bool on_surface = distance != 0 && distance < 1000 && roughness < 4000;
        const bool active = mouse_enabled && joycon && on_surface;
        constexpr u8 MouseLeft = 1, MouseRight = 2, MouseMiddle = 4;
        if (!active) {
            if (c.mouse_active) {
                // Lifted off: let go of what was held.
                if (c.mouse_buttons & MouseLeft) SendMouse(MOUSEEVENTF_LEFTUP);
                if (c.mouse_buttons & MouseRight) SendMouse(MOUSEEVENTF_RIGHTUP);
                if (c.mouse_buttons & MouseMiddle) SendMouse(MOUSEEVENTF_MIDDLEUP);
                c.mouse_buttons = 0;
                c.mouse_active = false;
            }
            return false;
        }
        const bool left = c.kind == Kind::JoyconLeft;
        const u32 left_bit = 1u << (left ? L : R);
        const u32 right_bit = 1u << (left ? ZL : ZR);
        const u32 middle_bit = 1u << (left ? StickL : StickR);
        if (c.mouse_active) {
            const auto wrap = [](u16 from, u16 to) {
                int diff = (static_cast<int>(to) - static_cast<int>(from)) & 0xFFFF;
                return diff > 32768 ? diff - 65536 : diff;
            };
            const int dx = wrap(c.mouse_x, x);
            const int dy = wrap(c.mouse_y, y);
            if (dx != 0 || dy != 0) {
                SendMouse(MOUSEEVENTF_MOVE, dx, dy);
            }
            // The stick scrolls.
            const auto [stick_x, stick_y] =
                left ? c.left_stick.Apply(&data[10]) : c.right_stick.Apply(&data[13]);
            if (std::abs(stick_y) > 0.2f) {
                SendMouse(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(static_cast<int>(stick_y * 40)));
            }
            const u8 now = ((buttons & left_bit) ? MouseLeft : 0) |
                           ((buttons & right_bit) ? MouseRight : 0) |
                           ((buttons & middle_bit) ? MouseMiddle : 0);
            const u8 pressed = now & ~c.mouse_buttons;
            const u8 released = c.mouse_buttons & ~now;
            if (pressed & MouseLeft) SendMouse(MOUSEEVENTF_LEFTDOWN);
            if (released & MouseLeft) SendMouse(MOUSEEVENTF_LEFTUP);
            if (pressed & MouseRight) SendMouse(MOUSEEVENTF_RIGHTDOWN);
            if (released & MouseRight) SendMouse(MOUSEEVENTF_RIGHTUP);
            if (pressed & MouseMiddle) SendMouse(MOUSEEVENTF_MIDDLEDOWN);
            if (released & MouseMiddle) SendMouse(MOUSEEVENTF_MIDDLEUP);
            c.mouse_buttons = now;
        }
        c.mouse_x = x;
        c.mouse_y = y;
        c.mouse_active = true;
        buttons &= ~(left_bit | right_bit | middle_bit);
        return true;
    }

    void OnInput(Controller& c, const std::vector<u8>& data) {
        if (data.size() < 60) {
            return;
        }
        const auto identifier = IdentifierOf(c.port, c.kind);
        u32 buttons = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
        const bool mouse = Mouse(c, data, buttons); // takes the buttons it uses
        const u32 changed = buttons ^ c.previous_buttons;
        for (int button = 0; button < ButtonCount; ++button) {
            if ((changed >> button) & 1) {
                engine.SetButton(identifier, button, ((buttons >> button) & 1) != 0);
            }
        }
        c.previous_buttons = buttons;

        if (c.kind != Kind::JoyconRight) {
            const auto [x, y] = c.left_stick.Apply(&data[10]);
            engine.SetAxis(identifier, LeftX, mouse ? 0.0f : x);
            engine.SetAxis(identifier, LeftY, mouse ? 0.0f : y);
        }
        if (c.kind != Kind::JoyconLeft) {
            const auto [x, y] = c.right_stick.Apply(&data[13]);
            engine.SetAxis(identifier, RightX, mouse ? 0.0f : x);
            engine.SetAxis(identifier, RightY, mouse ? 0.0f : y);
        }

        // Motion: about 4096 per g and a 2000 degrees per second range (the engine wants g and
        // turns per second).
        constexpr float AccelScale = 1.0f / 4096.0f;
        constexpr float GyroScale = 2000.0f / 32768.0f / 360.0f;
        const BasicMotion motion{
            .gyro_x = ReadS16(&data[54]) * GyroScale,
            .gyro_y = ReadS16(&data[56]) * GyroScale,
            .gyro_z = ReadS16(&data[58]) * GyroScale,
            .accel_x = ReadS16(&data[48]) * AccelScale,
            .accel_y = ReadS16(&data[50]) * AccelScale,
            .accel_z = ReadS16(&data[52]) * AccelScale,
            .delta_timestamp = 15000,
        };
        if (c.kind != Kind::JoyconRight) {
            engine.SetMotion(identifier, 0, motion);
        }
        if (c.kind != Kind::JoyconLeft) {
            engine.SetMotion(identifier, 1, motion);
        }

        // Battery, from its voltage: 3.3 V empty, 4.2 V full.
        const float volts = ReadU16(&data[31]) / 1000.0f;
        const int percent = std::clamp(static_cast<int>((volts - 3.3f) / 0.9f * 100.0f), 0, 100);
        if (volts > 1.0f && std::abs(percent - c.battery.load()) >= 5) {
            c.battery = percent;
            engine.SetBattery(identifier, percent > 70   ? Common::Input::BatteryLevel::Full
                                          : percent > 35 ? Common::Input::BatteryLevel::Medium
                                          : percent > 15 ? Common::Input::BatteryLevel::Low
                                                         : Common::Input::BatteryLevel::Critical);
        }
    }

    std::shared_ptr<Controller> Find(const PadIdentifier& identifier) const {
        if (identifier.pad >= slots.size() || identifier.port >= MaxPorts) {
            return nullptr;
        }
        std::scoped_lock lock{mutex};
        return slots[identifier.pad][identifier.port];
    }

    static void PutVibration(std::vector<u8>& out, float amplitude, u16 frequency) {
        // 9 bits of frequency, a tone bit, 10 bits of amplitude, twice (low and high band).
        const u64 amp = static_cast<u64>(std::clamp(amplitude, 0.0f, 1.0f) * 0x3FF * 0.85f);
        const u64 band = (frequency & 0x1FF) | (amp << 10);
        const u64 value = band | (((0x1e1 & 0x1FF) | (amp << 10)) << 20);
        for (int i = 0; i < 5; ++i) {
            out.push_back(static_cast<u8>(value >> (8 * i)));
        }
    }

    Common::Input::DriverResult Vibrate(const PadIdentifier& identifier,
                                        const Common::Input::VibrationStatus& vibration) {
        const auto c = Find(identifier);
        if (c == nullptr || c->vibration == nullptr) {
            return Common::Input::DriverResult::NotSupported;
        }
        const float amplitude = std::max(vibration.low_amplitude, vibration.high_amplitude);
        std::vector<u8> motor{static_cast<u8>(0x50 + (c->vibration_id++ & 0x0F))};
        PutVibration(motor, amplitude, 0x0e1);
        PutVibration(motor, 0.0f, 0x0e1);
        PutVibration(motor, 0.0f, 0x0e1);
        std::vector<u8> packet{0x00};
        packet.insert(packet.end(), motor.begin(), motor.end());
        if (c->kind == Kind::Pro || c->kind == Kind::GameCube) {
            packet.insert(packet.end(), motor.begin(), motor.end()); // both grips
        }
        try {
            // Not waited for: the game's thread must not stall on the radio.
            c->vibration.WriteValueAsync(BufferOf(packet), GattWriteOption::WriteWithoutResponse);
        } catch (const winrt::hresult_error&) {
            return Common::Input::DriverResult::ErrorWritingData;
        }
        return Common::Input::DriverResult::Success;
    }
};

#else

struct Switch2Controllers::Impl {
    explicit Impl(Switch2Controllers&) {}
};

#endif

Switch2Controllers::Switch2Controllers(std::string input_engine_)
    : InputEngine{std::move(input_engine_)}, impl{std::make_unique<Impl>(*this)} {}

Switch2Controllers::~Switch2Controllers() = default;

std::vector<Switch2Controllers::Status> Switch2Controllers::GetStatus() const {
    std::vector<Status> list;
#ifdef _WIN32
    std::scoped_lock lock{impl->mutex};
    for (const auto& kind_slots : impl->slots) {
        for (const auto& c : kind_slots) {
            if (c != nullptr && c->alive) {
                list.push_back({c->kind, c->port, AddressText(c->address), c->battery.load(),
                                c->paired_now});
            }
        }
    }
#endif
    return list;
}

std::string Switch2Controllers::BluetoothProblem() const {
#ifdef _WIN32
    std::scoped_lock lock{impl->mutex};
    return impl->problem;
#else
    return "Switch 2 controllers are paired from PhoenixEmu on Windows only for now.";
#endif
}

void Switch2Controllers::SetMouseEnabled([[maybe_unused]] bool enabled) {
#ifdef _WIN32
    impl->mouse_enabled = enabled;
#endif
}

bool Switch2Controllers::IsMouseEnabled() const {
#ifdef _WIN32
    return impl->mouse_enabled;
#else
    return false;
#endif
}

std::vector<Common::ParamPackage> Switch2Controllers::GetInputDevices() const {
    std::vector<Common::ParamPackage> devices;
    const auto param_of = [this](std::size_t port, Kind kind) {
        const auto identifier = IdentifierOf(port, kind);
        return Common::ParamPackage{{"engine", GetEngineName()},
                                    {"guid", identifier.guid.RawString()},
                                    {"port", std::to_string(identifier.port)},
                                    {"pad", std::to_string(identifier.pad)}};
    };
    const auto list = GetStatus();
    for (const Status& status : list) {
        auto param = param_of(status.port, status.kind);
        param.Set("display", fmt::format("{} {}", KindName(status.kind), status.port + 1));
        devices.push_back(std::move(param));
    }
    // A left and a right Joy-Con 2 on the same port, as one controller.
    for (std::size_t port = 0; port < MaxPorts; ++port) {
        const auto has = [&](Kind kind) {
            return std::ranges::any_of(
                list, [&](const Status& s) { return s.kind == kind && s.port == port; });
        };
        if (!has(Kind::JoyconLeft) || !has(Kind::JoyconRight)) {
            continue;
        }
        auto param = param_of(port, Kind::JoyconLeft);
        param.Set("guid2", param_of(port, Kind::JoyconRight).Get("guid", ""));
        param.Set("pad", std::to_string(static_cast<std::size_t>(Kind::Dual)));
        param.Set("display", fmt::format("{} {}", KindName(Kind::Dual), port + 1));
        devices.push_back(std::move(param));
    }
    return devices;
}

ButtonMapping Switch2Controllers::GetButtonMappingForDevice(const Common::ParamPackage& params) {
    if (!params.Has("port")) {
        return {};
    }
    // The Switch button, its bit, and the Joy-Con it is on (true: right) for a pair.
    static constexpr std::array<std::tuple<Settings::NativeButton::Values, int, bool>, 18> table{{
        {Settings::NativeButton::A, A, true},
        {Settings::NativeButton::B, B, true},
        {Settings::NativeButton::X, X, true},
        {Settings::NativeButton::Y, Y, true},
        {Settings::NativeButton::DLeft, Left, false},
        {Settings::NativeButton::DUp, Up, false},
        {Settings::NativeButton::DRight, Right, false},
        {Settings::NativeButton::DDown, Down, false},
        {Settings::NativeButton::L, L, false},
        {Settings::NativeButton::R, R, true},
        {Settings::NativeButton::ZL, ZL, false},
        {Settings::NativeButton::ZR, ZR, true},
        {Settings::NativeButton::Plus, Plus, true},
        {Settings::NativeButton::Minus, Minus, false},
        {Settings::NativeButton::Home, Home, true},
        {Settings::NativeButton::Screenshot, Capture, false},
        {Settings::NativeButton::LStick, StickL, false},
        {Settings::NativeButton::RStick, StickR, true},
    }};
    const std::size_t port = static_cast<std::size_t>(params.Get("port", 0));
    const auto pad = static_cast<Kind>(params.Get("pad", 0));
    const auto param_of = [&](Kind kind, int button) {
        const auto identifier = IdentifierOf(port, kind);
        Common::ParamPackage button_params{{"engine", GetEngineName()},
                                           {"guid", identifier.guid.RawString()},
                                           {"port", std::to_string(port)},
                                           {"pad", std::to_string(identifier.pad)}};
        button_params.Set("button", button);
        return button_params;
    };
    ButtonMapping mapping;
    for (const auto& [native, bit, right] : table) {
        const Kind kind = pad == Kind::Dual ? (right ? Kind::JoyconRight : Kind::JoyconLeft) : pad;
        mapping.insert_or_assign(native, param_of(kind, bit));
    }
    if (pad == Kind::JoyconLeft) {
        mapping.insert_or_assign(Settings::NativeButton::SLLeft, param_of(pad, SlLeft));
        mapping.insert_or_assign(Settings::NativeButton::SRLeft, param_of(pad, SrLeft));
    } else if (pad == Kind::JoyconRight) {
        mapping.insert_or_assign(Settings::NativeButton::SLRight, param_of(pad, SlRight));
        mapping.insert_or_assign(Settings::NativeButton::SRRight, param_of(pad, SrRight));
    }
    return mapping;
}

AnalogMapping Switch2Controllers::GetAnalogMappingForDevice(const Common::ParamPackage& params) {
    if (!params.Has("port")) {
        return {};
    }
    const std::size_t port = static_cast<std::size_t>(params.Get("port", 0));
    auto pad_left = static_cast<Kind>(params.Get("pad", 0));
    auto pad_right = pad_left;
    if (pad_left == Kind::Dual) {
        pad_left = Kind::JoyconLeft;
        pad_right = Kind::JoyconRight;
    }
    const auto param_of = [&](Kind kind, int axis_x, int axis_y) {
        const auto identifier = IdentifierOf(port, kind);
        Common::ParamPackage analog{{"engine", GetEngineName()},
                                    {"guid", identifier.guid.RawString()},
                                    {"port", std::to_string(port)},
                                    {"pad", std::to_string(identifier.pad)}};
        analog.Set("axis_x", axis_x);
        analog.Set("axis_y", axis_y);
        return analog;
    };
    AnalogMapping mapping;
    // A Joy-Con on its own uses its one stick as the left stick.
    if (pad_left == Kind::JoyconRight && pad_right == Kind::JoyconRight) {
        mapping.insert_or_assign(Settings::NativeAnalog::LStick,
                                 param_of(Kind::JoyconRight, RightX, RightY));
        return mapping;
    }
    mapping.insert_or_assign(Settings::NativeAnalog::LStick, param_of(pad_left, LeftX, LeftY));
    if (pad_right != Kind::JoyconLeft) {
        mapping.insert_or_assign(Settings::NativeAnalog::RStick,
                                 param_of(pad_right, RightX, RightY));
    }
    return mapping;
}

MotionMapping Switch2Controllers::GetMotionMappingForDevice(const Common::ParamPackage& params) {
    if (!params.Has("port")) {
        return {};
    }
    const std::size_t port = static_cast<std::size_t>(params.Get("port", 0));
    auto pad_left = static_cast<Kind>(params.Get("pad", 0));
    auto pad_right = pad_left;
    if (pad_left == Kind::Dual) {
        pad_left = Kind::JoyconLeft;
        pad_right = Kind::JoyconRight;
    }
    const auto param_of = [&](Kind kind, int motion) {
        const auto identifier = IdentifierOf(port, kind);
        Common::ParamPackage motion_params{{"engine", GetEngineName()},
                                           {"guid", identifier.guid.RawString()},
                                           {"port", std::to_string(port)},
                                           {"pad", std::to_string(identifier.pad)}};
        motion_params.Set("motion", motion);
        return motion_params;
    };
    MotionMapping mapping;
    mapping.insert_or_assign(Settings::NativeMotion::MotionLeft,
                             param_of(pad_left, pad_left == Kind::JoyconRight ? 1 : 0));
    mapping.insert_or_assign(Settings::NativeMotion::MotionRight,
                             param_of(pad_right, pad_right == Kind::JoyconLeft ? 0 : 1));
    return mapping;
}

Common::Input::ButtonNames Switch2Controllers::GetUIName(const Common::ParamPackage& params) const {
    using Common::Input::ButtonNames;
    if (params.Has("button")) {
        switch (params.Get("button", 0)) {
        case Left:
            return ButtonNames::ButtonLeft;
        case Right:
            return ButtonNames::ButtonRight;
        case Down:
            return ButtonNames::ButtonDown;
        case Up:
            return ButtonNames::ButtonUp;
        case SlLeft:
        case SlRight:
            return ButtonNames::TriggerSL;
        case SrLeft:
        case SrRight:
            return ButtonNames::TriggerSR;
        case L:
            return ButtonNames::TriggerL;
        case R:
            return ButtonNames::TriggerR;
        case ZL:
            return ButtonNames::TriggerZL;
        case ZR:
            return ButtonNames::TriggerZR;
        case A:
            return ButtonNames::ButtonA;
        case B:
            return ButtonNames::ButtonB;
        case X:
            return ButtonNames::ButtonX;
        case Y:
            return ButtonNames::ButtonY;
        case Plus:
            return ButtonNames::ButtonPlus;
        case Minus:
            return ButtonNames::ButtonMinus;
        case Home:
            return ButtonNames::ButtonHome;
        case Capture:
            return ButtonNames::ButtonCapture;
        case StickL:
            return ButtonNames::ButtonStickL;
        case StickR:
            return ButtonNames::ButtonStickR;
        default:
            return ButtonNames::Value;
        }
    }
    if (params.Has("axis") || params.Has("axis_x")) {
        return ButtonNames::Value;
    }
    return ButtonNames::Engine;
}

Common::Input::DriverResult Switch2Controllers::SetVibration(
    const PadIdentifier& identifier, const Common::Input::VibrationStatus& vibration) {
#ifdef _WIN32
    return impl->Vibrate(identifier, vibration);
#else
    return Common::Input::DriverResult::NotSupported;
#endif
}

bool Switch2Controllers::IsVibrationEnabled(const PadIdentifier& identifier) {
#ifdef _WIN32
    const auto c = impl->Find(identifier);
    return c != nullptr && c->vibration != nullptr;
#else
    return false;
#endif
}

} // namespace InputCommon
