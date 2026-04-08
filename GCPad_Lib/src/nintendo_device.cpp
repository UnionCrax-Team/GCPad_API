#include "nintendo_device.h"
#include "hid_device.h"
#include <iostream>
#include <cstring>
#include <thread>

namespace gcpad {
namespace internal {

class NintendoDevice : public GamepadDevice {
public:
    NintendoDevice(std::unique_ptr<HidDevice> hid_device, int index);
    ~NintendoDevice() override;

    int getIndex() const override { return index_; }
    std::string getName() const override { return "Nintendo Controller"; }
    std::string getSerialNumber() const override;

    const GamepadState& getState() const override { return state_; }
    GamepadState getRemappedState() const override;
    bool updateState() override;

    bool setLED(const Color& /*color*/) override { return false; }
    bool setRumble(const Rumble& rumble) override;

    void setRemapper(std::shared_ptr<Remapper> remapper) override;
    bool isConnected() const override { return connected_; }

private:
    std::unique_ptr<HidDevice> hid_device_;
    int index_;
    bool connected_;
    bool initialized_;
    GamepadState state_;
    std::shared_ptr<Remapper> remapper_;
    uint8_t output_counter_;

    static constexpr uint16_t NINTENDO_VID = 0x057E;
    static constexpr uint16_t SWITCH_PRO_PID = 0x2009;
    static constexpr uint16_t JOYCON_L_PID = 0x2006;
    static constexpr uint16_t JOYCON_R_PID = 0x2007;

    bool initialize_controller();
    bool send_subcommand(uint8_t subcmd, const uint8_t* data, size_t data_len);
    bool parse_standard_report(const std::vector<uint8_t>& report);
    bool parse_simple_report(const std::vector<uint8_t>& report);
};

NintendoDevice::NintendoDevice(std::unique_ptr<HidDevice> hid_device, int index)
    : hid_device_(std::move(hid_device)), index_(index), connected_(false),
      initialized_(false), remapper_(nullptr), output_counter_(0) {
    state_.reset();
}

NintendoDevice::~NintendoDevice() {
    if (connected_) hid_device_->close();
}

std::string NintendoDevice::getSerialNumber() const {
    if (!connected_) return "";
    return hid_device_->get_serial_number_string();
}

GamepadState NintendoDevice::getRemappedState() const {
    if (remapper_) return remapper_->apply(state_);
    return state_;
}

void NintendoDevice::setRemapper(std::shared_ptr<Remapper> remapper) {
    remapper_ = std::move(remapper);
}

bool NintendoDevice::send_subcommand(uint8_t subcmd, const uint8_t* data, size_t data_len) {
    // Sub-command output report: 0x01
    // [0] = 0x01 (report ID)
    // [1] = output counter (incremented)
    // [2-9] = rumble data (neutral)
    // [10] = sub-command ID
    // [11+] = sub-command data
    std::vector<uint8_t> report(49, 0);
    report[0] = 0x01;
    report[1] = output_counter_++;

    // Neutral rumble (4 bytes left + 4 bytes right)
    report[2] = 0x00; report[3] = 0x01; report[4] = 0x40; report[5] = 0x40;
    report[6] = 0x00; report[7] = 0x01; report[8] = 0x40; report[9] = 0x40;

    report[10] = subcmd;
    for (size_t i = 0; i < data_len && (11 + i) < report.size(); ++i) {
        report[11 + i] = data[i];
    }

    return hid_device_->write(report);
}

bool NintendoDevice::initialize_controller() {
    // Step 1: Request standard full mode (sub-command 0x03, arg 0x30)
    // This switches from the simple HID report (0x3F) to the standard
    // full input report (0x30) which has stick calibration data,
    // 6-axis IMU data, and full button state.
    uint8_t mode = 0x30; // Standard full report mode
    if (!send_subcommand(0x03, &mode, 1)) {
        // If sub-command fails, we'll still try reading simple reports
        return true;
    }

    // Small delay for the controller to switch modes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Step 2: Enable IMU (sub-command 0x40, arg 0x01)
    uint8_t imu_enable = 0x01;
    send_subcommand(0x40, &imu_enable, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    initialized_ = true;
    return true;
}

bool NintendoDevice::updateState() {
    if (!connected_) {
        if (!hid_device_->open()) return false;
        connected_ = true;
        state_.is_connected = true;

        // Initialize controller to full report mode
        initialize_controller();
    }

    std::vector<uint8_t> report;
    if (!hid_device_->read(report)) {
        connected_ = false;
        state_.is_connected = false;
        return false;
    }

    if (report.empty()) return false;

    // Report ID 0x30 = standard full input report (after initialization)
    // Report ID 0x21 = sub-command reply (also contains input data)
    // Report ID 0x3F = simple HID report (before initialization)
    if (report[0] == 0x30 || report[0] == 0x21) {
        return parse_standard_report(report);
    } else if (report[0] == 0x3F) {
        return parse_simple_report(report);
    }

    // Unknown report ID — still valid update, just skip
    return true;
}

bool NintendoDevice::parse_standard_report(const std::vector<uint8_t>& report) {
    // Standard full report (0x30 or 0x21):
    // [0] = Report ID
    // [1] = Timer
    // [2] = Battery/Connection info
    // [3] = Button byte 1 (right buttons: Y, X, B, A, SR, SL, R, ZR)
    // [4] = Button byte 2 (shared: Minus, Plus, RStick, LStick, Home, Capture)
    // [5] = Button byte 3 (left buttons: Down, Up, Right, Left, SR, SL, L, ZL)
    // [6-8] = Left stick (12-bit X, 12-bit Y)
    // [9-11] = Right stick (12-bit X, 12-bit Y)
    if (report.size() < 12) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    // Battery level from byte 2
    uint8_t battery_info = report[2];
    state_.battery_level = ((battery_info >> 5) & 0x07) / 4.0f;
    state_.is_charging = (battery_info & 0x10) != 0;

    // Parse buttons
    uint8_t right_buttons = report[3];
    uint8_t shared_buttons = report[4];
    uint8_t left_buttons = report[5];

    // Map Nintendo buttons to our unified layout
    // Pro Controller: A=east, B=south, X=north, Y=west
    // We map to: A=south(B), B=east(A), X=west(Y), Y=north(X)
    state_.buttons[static_cast<size_t>(Button::A)] = (right_buttons & 0x08) != 0; // Nintendo A (east) -> our B
    state_.buttons[static_cast<size_t>(Button::B)] = (right_buttons & 0x04) != 0; // Nintendo B (south) -> our A
    state_.buttons[static_cast<size_t>(Button::X)] = (right_buttons & 0x02) != 0; // Nintendo X (north) -> our Y
    state_.buttons[static_cast<size_t>(Button::Y)] = (right_buttons & 0x01) != 0; // Nintendo Y (west) -> our X

    state_.buttons[static_cast<size_t>(Button::R1)] = (right_buttons & 0x40) != 0; // R
    state_.buttons[static_cast<size_t>(Button::R2)] = (right_buttons & 0x80) != 0; // ZR

    state_.buttons[static_cast<size_t>(Button::L1)] = (left_buttons & 0x40) != 0; // L
    state_.buttons[static_cast<size_t>(Button::L2)] = (left_buttons & 0x80) != 0; // ZL

    // D-Pad (left buttons, bits 0-3)
    state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = (left_buttons & 0x01) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = (left_buttons & 0x02) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (left_buttons & 0x04) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = (left_buttons & 0x08) != 0;

    // Shared buttons
    state_.buttons[static_cast<size_t>(Button::Select)] = (shared_buttons & 0x01) != 0; // Minus
    state_.buttons[static_cast<size_t>(Button::Start)]  = (shared_buttons & 0x02) != 0; // Plus
    state_.buttons[static_cast<size_t>(Button::R3)]     = (shared_buttons & 0x04) != 0; // R Stick click
    state_.buttons[static_cast<size_t>(Button::L3)]     = (shared_buttons & 0x08) != 0; // L Stick click
    state_.buttons[static_cast<size_t>(Button::Guide)]  = (shared_buttons & 0x10) != 0; // Home

    // Left stick (12-bit, bytes 6-8)
    uint16_t lx_raw = report[6] | ((report[7] & 0x0F) << 8);
    uint16_t ly_raw = (report[7] >> 4) | (report[8] << 4);

    // Right stick (12-bit, bytes 9-11)
    uint16_t rx_raw = report[9] | ((report[10] & 0x0F) << 8);
    uint16_t ry_raw = (report[10] >> 4) | (report[11] << 4);

    // Center at ~2048, range 0-4095. Map to -1.0 to 1.0
    state_.axes[static_cast<size_t>(Axis::LeftX)]  = (lx_raw - 2048.0f) / 2048.0f;
    state_.axes[static_cast<size_t>(Axis::LeftY)]  = -((ly_raw - 2048.0f) / 2048.0f); // Y inverted
    state_.axes[static_cast<size_t>(Axis::RightX)] = (rx_raw - 2048.0f) / 2048.0f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = -((ry_raw - 2048.0f) / 2048.0f);

    // Digital triggers (Switch has digital, not analog triggers)
    state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = state_.buttons[static_cast<size_t>(Button::L2)] ? 1.0f : 0.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = state_.buttons[static_cast<size_t>(Button::R2)] ? 1.0f : 0.0f;

    // IMU data at bytes 13-24 (if present in 0x30 reports), converted to physical units
    if (report[0] == 0x30 && report.size() >= 49) {
        // First sample (of 3) at bytes 13-24
        state_.accel.x = static_cast<float>(static_cast<int16_t>(report[13] | (report[14] << 8))) * calibration::NINTENDO_ACCEL_SCALE;
        state_.accel.y = static_cast<float>(static_cast<int16_t>(report[15] | (report[16] << 8))) * calibration::NINTENDO_ACCEL_SCALE;
        state_.accel.z = static_cast<float>(static_cast<int16_t>(report[17] | (report[18] << 8))) * calibration::NINTENDO_ACCEL_SCALE;
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>(report[19] | (report[20] << 8))) * calibration::NINTENDO_GYRO_SCALE;
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>(report[21] | (report[22] << 8))) * calibration::NINTENDO_GYRO_SCALE;
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>(report[23] | (report[24] << 8))) * calibration::NINTENDO_GYRO_SCALE;
    }

    return true;
}

bool NintendoDevice::parse_simple_report(const std::vector<uint8_t>& report) {
    // Simple HID report (0x3F): sent before controller is initialized
    // [0] = 0x3F
    // [1] = buttons1 (hat + face)
    // [2] = buttons2 (shoulder + misc)
    // [3] = left stick X (0-255, center 128)
    // [4] = left stick Y (0-255, center 128)
    // [5] = right stick X
    // [6] = right stick Y
    if (report.size() < 7) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    uint8_t buttons1 = report[1];
    uint8_t buttons2 = report[2];

    // D-Pad (hat switch, lower nibble of buttons1)
    uint8_t hat = buttons1 & 0x0F;
    state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = (hat == 0 || hat == 1 || hat == 7);
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (hat >= 1 && hat <= 3);
    state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = (hat >= 3 && hat <= 5);
    state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = (hat >= 5 && hat <= 7);

    // Face buttons (upper nibble of buttons1)
    state_.buttons[static_cast<size_t>(Button::A)] = (buttons1 & 0x10) != 0;
    state_.buttons[static_cast<size_t>(Button::B)] = (buttons1 & 0x20) != 0;
    state_.buttons[static_cast<size_t>(Button::X)] = (buttons1 & 0x40) != 0;
    state_.buttons[static_cast<size_t>(Button::Y)] = (buttons1 & 0x80) != 0;

    state_.buttons[static_cast<size_t>(Button::L1)]     = (buttons2 & 0x01) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)]     = (buttons2 & 0x02) != 0;
    state_.buttons[static_cast<size_t>(Button::L2)]     = (buttons2 & 0x04) != 0;
    state_.buttons[static_cast<size_t>(Button::R2)]     = (buttons2 & 0x08) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] = (buttons2 & 0x10) != 0;
    state_.buttons[static_cast<size_t>(Button::Start)]  = (buttons2 & 0x20) != 0;
    state_.buttons[static_cast<size_t>(Button::L3)]     = (buttons2 & 0x40) != 0;
    state_.buttons[static_cast<size_t>(Button::R3)]     = (buttons2 & 0x80) != 0;

    // Analog sticks (8-bit)
    state_.axes[static_cast<size_t>(Axis::LeftX)]  = (report[3] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::LeftY)]  = (report[4] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = (report[5] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = (report[6] - 128.0f) / 127.5f;

    state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = state_.buttons[static_cast<size_t>(Button::L2)] ? 1.0f : 0.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = state_.buttons[static_cast<size_t>(Button::R2)] ? 1.0f : 0.0f;

    return true;
}

bool NintendoDevice::setRumble(const Rumble& rumble) {
    if (!connected_) return false;

    // HD rumble output report: 0x10
    // [0] = 0x10
    // [1] = output counter
    // [2-5] = left rumble data (4 bytes)
    // [6-9] = right rumble data (4 bytes)
    std::vector<uint8_t> report(10, 0);
    report[0] = 0x10;
    report[1] = output_counter_++;

    if (rumble.left_motor > 0) {
        // Simplified rumble encoding (low frequency)
        report[2] = 0x00; report[3] = 0x01;
        report[4] = rumble.left_motor >> 1; report[5] = 0x40;
    } else {
        report[2] = 0x00; report[3] = 0x01; report[4] = 0x40; report[5] = 0x40;
    }

    if (rumble.right_motor > 0) {
        // High frequency rumble
        report[6] = 0x00; report[7] = 0x01;
        report[8] = rumble.right_motor >> 1; report[9] = 0x40;
    } else {
        report[6] = 0x00; report[7] = 0x01; report[8] = 0x40; report[9] = 0x40;
    }

    return hid_device_->write(report);
}

std::vector<NintendoDeviceInfo> getNintendoDeviceInfos() {
    return {
        {0x057E, 0x2009, "Switch Pro Controller"},
        {0x057E, 0x2006, "Joy-Con Left"},
        {0x057E, 0x2007, "Joy-Con Right"},
    };
}

std::unique_ptr<GamepadDevice> createNintendoDevice(std::unique_ptr<HidDevice> hid_device, int index) {
    return std::make_unique<NintendoDevice>(std::move(hid_device), index);
}

} // namespace internal
} // namespace gcpad
