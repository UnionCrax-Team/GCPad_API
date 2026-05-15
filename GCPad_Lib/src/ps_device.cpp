#include "ps_device.h"
#include "hid_device.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>

// Parse a 4-byte Sony touchpad contact record.
// Byte layout: [active_id][x_lo][x_hi_nibble|y_lo_nibble][y_hi]
// active = bit7 of byte0 is 0 (finger present), 1 (no finger)
static void parse_touch_point(const uint8_t* p,
                               bool& active, uint16_t& x, uint16_t& y) {
    active = (p[0] & 0x80) == 0;
    x = static_cast<uint16_t>(p[1] | ((p[2] & 0x0F) << 8));
    y = static_cast<uint16_t>((p[2] >> 4) | (p[3] << 4));
}

namespace gcpad {
namespace internal {

// PlayStation device class - handles DS4 and DualSense
class PlayStationDevice : public GamepadDevice {
public:
    PlayStationDevice(std::unique_ptr<HidDevice> hid_device, int index);
    ~PlayStationDevice() override;

    // GamepadDevice interface
    int getIndex() const override { return index_; }
    std::string getName() const override;
    std::string getSerialNumber() const override;

    const GamepadState& getState() const override { return state_; }
    GamepadState getRemappedState() const override;
    bool updateState() override;

    bool setLED(const Color& color) override;
    bool setRumble(const Rumble& rumble) override;
    bool setTriggerEffect(bool right_trigger, const TriggerEffect& effect) override;
    bool setPlayerLEDs(uint8_t led_mask) override;

    void setRemapper(std::shared_ptr<Remapper> remapper) override;

    bool isConnected() const override { return connected_; }

private:
    std::unique_ptr<HidDevice> hid_device_;
    int index_;
    bool connected_;
    GamepadState state_;

    enum class ConnectionType { USB, Bluetooth };
    enum class ControllerModel { DS4, DualSense };

    ConnectionType connection_type_;
    ControllerModel model_;
    std::shared_ptr<Remapper> remapper_;

    // Store last output values for combined LED+rumble reports
    Color last_color_;
    Rumble last_rumble_;
    TriggerEffect last_right_trigger_effect_;
    TriggerEffect last_left_trigger_effect_;
    uint8_t last_player_leds_ = 0;

    // Internal methods
    bool detect_device_type();
    bool parse_ds4_usb(const std::vector<uint8_t>& report);
    bool parse_ds4_bt(const std::vector<uint8_t>& report);
    bool parse_dualsense_usb(const std::vector<uint8_t>& report);
    bool parse_dualsense_bt(const std::vector<uint8_t>& report);
    void parse_ds4_buttons(const uint8_t* data);
    void parse_dualsense_buttons(const uint8_t* data);
    std::vector<uint8_t> create_ds4_output_usb(const Color& color, const Rumble& rumble);
    std::vector<uint8_t> create_ds4_output_bt(const Color& color, const Rumble& rumble);
    std::vector<uint8_t> create_dualsense_output_usb(const Color& color, const Rumble& rumble);
    std::vector<uint8_t> create_dualsense_output_bt(const Color& color, const Rumble& rumble);

    // PS constants
    static constexpr uint16_t SONY_VID = 0x054C;
    static constexpr uint16_t DS4_V1_PID = 0x05C4;   // DualShock 4 v1
    static constexpr uint16_t DS4_V2_PID = 0x09CC;   // DualShock 4 v2
    static constexpr uint16_t DS5_PID = 0x0CE6;      // DualSense
    static constexpr uint16_t DS5_EDGE_PID = 0x0DF2; // DualSense Edge
};

PlayStationDevice::PlayStationDevice(std::unique_ptr<HidDevice> hid_device, int index)
    : hid_device_(std::move(hid_device)), index_(index), connected_(false),
      connection_type_(ConnectionType::USB), model_(ControllerModel::DS4),
      remapper_(nullptr) {
    state_.reset();
}

PlayStationDevice::~PlayStationDevice() {
    if (connected_) {
        hid_device_->close();
    }
}

std::string PlayStationDevice::getName() const {
    if (model_ == ControllerModel::DualSense) {
        return "DualSense Controller";
    }
    return "DualShock 4 Controller";
}

std::string PlayStationDevice::getSerialNumber() const {
    if (!connected_) return "";
    return hid_device_->get_serial_number_string();
}

GamepadState PlayStationDevice::getRemappedState() const {
    if (remapper_) {
        return remapper_->apply(state_);
    }
    return state_;
}

void PlayStationDevice::setRemapper(std::shared_ptr<Remapper> remapper) {
    remapper_ = std::move(remapper);
}

bool PlayStationDevice::updateState() {
    if (!connected_) {
        if (hid_device_->open() && detect_device_type()) {
            connected_ = true;
            state_.is_connected = true;
        } else {
            state_.is_connected = false;
            return false;
        }
    }

    std::vector<uint8_t> report;
    if (!hid_device_->read(report)) {
        // A single read failure is *not* enough to declare the device gone.
        // Windows' overlapped HID read times out at ~100ms, which can happen
        // any time the controller has nothing new to report (no buttons,
        // sticks centered, no IMU motion above sensor noise). Only treat
        // the device as disconnected if the underlying handle is actually
        // closed -- otherwise just keep the last known state and try again
        // next tick.
        if (!hid_device_->is_open()) {
            connected_ = false;
            state_.is_connected = false;
            return false;
        }
        // Keep last state, return false to signal "no fresh data" but stay alive.
        return false;
    }

    if (model_ == ControllerModel::DS4) {
        if (connection_type_ == ConnectionType::USB) {
            return parse_ds4_usb(report);
        } else {
            return parse_ds4_bt(report);
        }
    } else {
        if (connection_type_ == ConnectionType::USB) {
            return parse_dualsense_usb(report);
        } else {
            return parse_dualsense_bt(report);
        }
    }
}

bool PlayStationDevice::setLED(const Color& color) {
    if (!connected_) return false;
    last_color_ = color;

    std::vector<uint8_t> report;
    if (model_ == ControllerModel::DS4) {
        report = (connection_type_ == ConnectionType::USB)
            ? create_ds4_output_usb(color, last_rumble_)
            : create_ds4_output_bt(color, last_rumble_);
    } else {
        report = (connection_type_ == ConnectionType::USB)
            ? create_dualsense_output_usb(color, last_rumble_)
            : create_dualsense_output_bt(color, last_rumble_);
    }
    return hid_device_->write(report);
}

bool PlayStationDevice::setRumble(const Rumble& rumble) {
    if (!connected_) return false;
    last_rumble_ = rumble;

    std::vector<uint8_t> report;
    if (model_ == ControllerModel::DS4) {
        report = (connection_type_ == ConnectionType::USB)
            ? create_ds4_output_usb(last_color_, rumble)
            : create_ds4_output_bt(last_color_, rumble);
    } else {
        report = (connection_type_ == ConnectionType::USB)
            ? create_dualsense_output_usb(last_color_, rumble)
            : create_dualsense_output_bt(last_color_, rumble);
    }
    return hid_device_->write(report);
}

bool PlayStationDevice::setTriggerEffect(bool right_trigger, const TriggerEffect& effect) {
    if (!connected_ || model_ != ControllerModel::DualSense) return false;
    if (right_trigger) {
        last_right_trigger_effect_ = effect;
    } else {
        last_left_trigger_effect_ = effect;
    }
    // Send a full output report with current state
    std::vector<uint8_t> report;
    report = (connection_type_ == ConnectionType::USB)
        ? create_dualsense_output_usb(last_color_, last_rumble_)
        : create_dualsense_output_bt(last_color_, last_rumble_);
    return hid_device_->write(report);
}

bool PlayStationDevice::setPlayerLEDs(uint8_t led_mask) {
    if (!connected_ || model_ != ControllerModel::DualSense) return false;
    last_player_leds_ = led_mask & 0x1F; // 5 LEDs
    std::vector<uint8_t> report;
    report = (connection_type_ == ConnectionType::USB)
        ? create_dualsense_output_usb(last_color_, last_rumble_)
        : create_dualsense_output_bt(last_color_, last_rumble_);
    return hid_device_->write(report);
}

bool PlayStationDevice::detect_device_type() {
    auto caps = hid_device_->get_capabilities();
    auto attributes = hid_device_->get_attributes();

    // Determine model
    if (attributes.product_id == DS5_PID || attributes.product_id == DS5_EDGE_PID) {
        model_ = ControllerModel::DualSense;
    } else {
        model_ = ControllerModel::DS4;
    }

    // DS4 v1 (0x05C4) is typically USB; DS4 v2 (0x09CC) can be either USB or BT.
    // DualSense (0x0CE6) can be either USB or BT.
    // We distinguish USB vs BT by the input report length.
    //
    // NOTE: On Windows, HIDP_CAPS::InputReportByteLength includes the report
    // ID byte. So a DS4 USB report (64 data bytes + 1 report-ID byte) shows
    // up here as 65, not 64. The previous threshold of `> 64` was wrong --
    // it misclassified every USB DS4 as Bluetooth, fed it to parse_ds4_bt(),
    // which then rejected the report because report[0] was 0x01 instead of
    // 0x11. Use `> 65` so USB stays USB and only true BT reports (78+ for
    // DS4 simple, 547 for full BT, 78 for DualSense BT) get the BT path.
    if (caps.input_report_byte_length > 65) {
        connection_type_ = ConnectionType::Bluetooth;
    } else {
        connection_type_ = ConnectionType::USB;
    }

    return true;
}

// Parse DS4 common button data from a base pointer (byte 0 = left stick X)
void PlayStationDevice::parse_ds4_buttons(const uint8_t* data) {
    // data[0] = LX, data[1] = LY, data[2] = RX, data[3] = RY
    state_.axes[static_cast<size_t>(Axis::LeftX)]  = (data[0] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::LeftY)]  = (data[1] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = (data[2] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = (data[3] - 128.0f) / 127.5f;

    // data[4] = buttons1 (hat + face buttons), data[5] = buttons2, data[6] = counter+PS
    uint8_t buttons1 = data[4];
    uint8_t buttons2 = data[5];

    // Face buttons (based on DS4 HID descriptor)
    state_.buttons[static_cast<size_t>(Button::X)] = (buttons1 & 0x10) != 0; // Square
    state_.buttons[static_cast<size_t>(Button::A)] = (buttons1 & 0x20) != 0; // Cross
    state_.buttons[static_cast<size_t>(Button::B)] = (buttons1 & 0x40) != 0; // Circle
    state_.buttons[static_cast<size_t>(Button::Y)] = (buttons1 & 0x80) != 0; // Triangle

    // D-Pad (hat switch in low 4 bits)
    uint8_t dpad = buttons1 & 0x0F;
    state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = (dpad == 0 || dpad == 1 || dpad == 7);
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (dpad >= 1 && dpad <= 3);
    state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = (dpad >= 3 && dpad <= 5);
    state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = (dpad >= 5 && dpad <= 7);

    // Shoulder + options buttons
    state_.buttons[static_cast<size_t>(Button::L1)]     = (buttons2 & 0x01) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)]     = (buttons2 & 0x02) != 0;
    state_.buttons[static_cast<size_t>(Button::L2)]     = (buttons2 & 0x04) != 0;
    state_.buttons[static_cast<size_t>(Button::R2)]     = (buttons2 & 0x08) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] = (buttons2 & 0x10) != 0; // Share
    state_.buttons[static_cast<size_t>(Button::Start)]  = (buttons2 & 0x20) != 0; // Options
    state_.buttons[static_cast<size_t>(Button::L3)]     = (buttons2 & 0x40) != 0;
    state_.buttons[static_cast<size_t>(Button::R3)]     = (buttons2 & 0x80) != 0;

    // PS button
    state_.buttons[static_cast<size_t>(Button::Guide)]    = (data[6] & 0x01) != 0;
    // Touchpad click
    state_.buttons[static_cast<size_t>(Button::Touchpad)] = (data[6] & 0x02) != 0;

    // Triggers (analog)
    state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = data[7] / 255.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = data[8] / 255.0f;
}

bool PlayStationDevice::parse_ds4_usb(const std::vector<uint8_t>& report) {
    // DS4 USB: Report ID 0x01, 64 bytes total
    // Byte 0 = report ID (0x01)
    // Byte 1 = LX, 2 = LY, 3 = RX, 4 = RY
    // Byte 5 = buttons1, 6 = buttons2, 7 = PS/TP counter
    // Byte 8 = L2, 9 = R2
    // Bytes 13-24 = gyro/accel (6x int16 LE)
    // Byte 12 = battery
    if (report.size() < 64 || report[0] != 0x01) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    parse_ds4_buttons(&report[1]);

    // IMU data (gyro + accel) at bytes 13-24, converted to physical units
    if (report.size() >= 25) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[14] << 8) | report[13])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[16] << 8) | report[15])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[18] << 8) | report[17])) * calibration::SONY_GYRO_SCALE;
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[20] << 8) | report[19])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[22] << 8) | report[21])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[24] << 8) | report[23])) * calibration::SONY_ACCEL_SCALE;
    }

    // Battery
    if (report.size() >= 31) {
        uint8_t battery_raw = report[30];
        state_.battery_level = (battery_raw & 0x0F) / 10.0f;
        state_.is_charging = (battery_raw & 0x10) != 0;
    }

    // Touchpad finger positions — 2 touch records at bytes 35-42
    if (report.size() >= 43) {
        parse_touch_point(&report[35],
            state_.touchpad[0].active, state_.touchpad[0].x, state_.touchpad[0].y);
        parse_touch_point(&report[39],
            state_.touchpad[1].active, state_.touchpad[1].x, state_.touchpad[1].y);
    }

    return true;
}

bool PlayStationDevice::parse_ds4_bt(const std::vector<uint8_t>& report) {
    // DS4 Bluetooth: Report ID 0x11 ("enhanced" mode), 78+ bytes
    // The data layout is the same as USB but shifted by +2 bytes:
    // Byte 0 = report ID (0x11)
    // Byte 1-2 = protocol header
    // Byte 3 = LX, 4 = LY, 5 = RX, 6 = RY
    // Byte 7 = buttons1, 8 = buttons2, 9 = PS/TP counter
    // Byte 10 = L2, 11 = R2
    //
    // Some DS4 firmwares / pairing states emit report ID 0x01 over BT in
    // "simple" mode (no IMU, no touchpad). The first 10 bytes use the same
    // layout as the USB 0x01 report, so we just hand it off there.
    if (report.size() < 10) return false;
    if (report[0] == 0x01) {
        return parse_ds4_usb(report);
    }
    if (report[0] != 0x11) return false;
    if (report.size() < 32) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    // Button/axis data starts at byte 3 (offset +2 from USB)
    parse_ds4_buttons(&report[3]);

    // IMU at offset +2: bytes 15-26, converted to physical units
    if (report.size() >= 27) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[16] << 8) | report[15])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[18] << 8) | report[17])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[20] << 8) | report[19])) * calibration::SONY_GYRO_SCALE;
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[22] << 8) | report[21])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[24] << 8) | report[23])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[26] << 8) | report[25])) * calibration::SONY_ACCEL_SCALE;
    }

    // Battery at offset +2: byte 32
    if (report.size() >= 33) {
        uint8_t battery_raw = report[32];
        state_.battery_level = (battery_raw & 0x0F) / 10.0f;
        state_.is_charging = (battery_raw & 0x10) != 0;
    }

    // Touchpad finger positions — 2 touch records at bytes 37-44 (DS4 BT = USB + 2)
    if (report.size() >= 45) {
        parse_touch_point(&report[37],
            state_.touchpad[0].active, state_.touchpad[0].x, state_.touchpad[0].y);
        parse_touch_point(&report[41],
            state_.touchpad[1].active, state_.touchpad[1].x, state_.touchpad[1].y);
    }

    return true;
}

// Parse DualSense common button data from a base pointer
void PlayStationDevice::parse_dualsense_buttons(const uint8_t* data) {
    // DualSense USB report (after report ID):
    // data[0] = LX, data[1] = LY, data[2] = RX, data[3] = RY
    // data[4] = L2 trigger, data[5] = R2 trigger
    // data[7] = buttons1 (dpad + face), data[8] = buttons2
    // data[9] = PS/mute/TP

    state_.axes[static_cast<size_t>(Axis::LeftX)]  = (data[0] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::LeftY)]  = (data[1] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = (data[2] - 128.0f) / 127.5f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = (data[3] - 128.0f) / 127.5f;

    // Triggers (analog values)
    state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = data[4] / 255.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = data[5] / 255.0f;

    // Byte 7 (offset from base): lower 4 bits = dpad, upper 4 bits = face buttons
    uint8_t buttons1 = data[7];
    uint8_t buttons2 = data[8];

    // D-Pad (hat switch in low 4 bits of buttons1)
    uint8_t dpad = buttons1 & 0x0F;
    state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = (dpad == 0 || dpad == 1 || dpad == 7);
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (dpad >= 1 && dpad <= 3);
    state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = (dpad >= 3 && dpad <= 5);
    state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = (dpad >= 5 && dpad <= 7);

    // Face buttons (bits 4-7 of buttons1)
    state_.buttons[static_cast<size_t>(Button::X)] = (buttons1 & 0x10) != 0; // Square
    state_.buttons[static_cast<size_t>(Button::A)] = (buttons1 & 0x20) != 0; // Cross
    state_.buttons[static_cast<size_t>(Button::B)] = (buttons1 & 0x40) != 0; // Circle
    state_.buttons[static_cast<size_t>(Button::Y)] = (buttons1 & 0x80) != 0; // Triangle

    // Shoulder + options (buttons2)
    state_.buttons[static_cast<size_t>(Button::L1)]     = (buttons2 & 0x01) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)]     = (buttons2 & 0x02) != 0;
    state_.buttons[static_cast<size_t>(Button::L2)]     = (buttons2 & 0x04) != 0;
    state_.buttons[static_cast<size_t>(Button::R2)]     = (buttons2 & 0x08) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] = (buttons2 & 0x10) != 0; // Create
    state_.buttons[static_cast<size_t>(Button::Start)]  = (buttons2 & 0x20) != 0; // Options
    state_.buttons[static_cast<size_t>(Button::L3)]     = (buttons2 & 0x40) != 0;
    state_.buttons[static_cast<size_t>(Button::R3)]     = (buttons2 & 0x80) != 0;

    // PS button (byte 9, bit 0), Touchpad click (bit 1), Mute (bit 2)
    state_.buttons[static_cast<size_t>(Button::Guide)]    = (data[9] & 0x01) != 0;
    state_.buttons[static_cast<size_t>(Button::Touchpad)] = (data[9] & 0x02) != 0;
}

bool PlayStationDevice::parse_dualsense_usb(const std::vector<uint8_t>& report) {
    // DualSense USB: Report ID 0x01, 64 bytes
    // Byte 0 = report ID (0x01)
    // Byte 1 = LX, 2 = LY, 3 = RX, 4 = RY
    // Byte 5 = L2, 6 = R2
    // Byte 7 = counter
    // Byte 8 = buttons1 (dpad + face), 9 = buttons2, 10 = PS/mute/TP
    // Bytes 15-26 = gyro/accel
    if (report.size() < 64 || report[0] != 0x01) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    parse_dualsense_buttons(&report[1]);

    // Gyro/Accel at bytes 15-26, converted to physical units
    if (report.size() >= 27) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[16] << 8) | report[15])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[18] << 8) | report[17])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[20] << 8) | report[19])) * calibration::SONY_GYRO_SCALE;
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[22] << 8) | report[21])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[24] << 8) | report[23])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[26] << 8) | report[25])) * calibration::SONY_ACCEL_SCALE;
    }

    // Battery at byte 53
    if (report.size() >= 54) {
        uint8_t battery_raw = report[53];
        uint8_t battery_level = battery_raw & 0x0F;
        uint8_t battery_status = (battery_raw >> 4) & 0x0F;
        state_.battery_level = battery_level / 10.0f;
        state_.is_charging = (battery_status == 1 || battery_status == 2);
    }

    // Touchpad finger positions — 2 touch records at bytes 35-42
    if (report.size() >= 43) {
        parse_touch_point(&report[35],
            state_.touchpad[0].active, state_.touchpad[0].x, state_.touchpad[0].y);
        parse_touch_point(&report[39],
            state_.touchpad[1].active, state_.touchpad[1].x, state_.touchpad[1].y);
    }

    return true;
}

bool PlayStationDevice::parse_dualsense_bt(const std::vector<uint8_t>& report) {
    // DualSense Bluetooth: Report ID 0x31, 78 bytes
    // Byte 0 = report ID (0x31)
    // Byte 1 = sequence/counter
    // Byte 2 = LX, 3 = LY, 4 = RX, 5 = RY
    // Byte 6 = L2, 7 = R2
    // etc. (same layout as USB shifted by +1)
    if (report.size() < 10) return false;
    if (report[0] != 0x31) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    parse_dualsense_buttons(&report[2]);

    // Gyro/Accel at offset +1 from USB positions, converted to physical units
    if (report.size() >= 28) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[17] << 8) | report[16])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[19] << 8) | report[18])) * calibration::SONY_GYRO_SCALE;
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[21] << 8) | report[20])) * calibration::SONY_GYRO_SCALE;
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[23] << 8) | report[22])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[25] << 8) | report[24])) * calibration::SONY_ACCEL_SCALE;
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[27] << 8) | report[26])) * calibration::SONY_ACCEL_SCALE;
    }

    // Battery at byte 54
    if (report.size() >= 55) {
        uint8_t battery_raw = report[54];
        uint8_t battery_level = battery_raw & 0x0F;
        uint8_t battery_status = (battery_raw >> 4) & 0x0F;
        state_.battery_level = battery_level / 10.0f;
        state_.is_charging = (battery_status == 1 || battery_status == 2);
    }

    // Touchpad finger positions — 2 touch records at bytes 36-43 (DualSense BT = USB + 1)
    if (report.size() >= 44) {
        parse_touch_point(&report[36],
            state_.touchpad[0].active, state_.touchpad[0].x, state_.touchpad[0].y);
        parse_touch_point(&report[40],
            state_.touchpad[1].active, state_.touchpad[1].x, state_.touchpad[1].y);
    }

    return true;
}

// --- Output reports ---

std::vector<uint8_t> PlayStationDevice::create_ds4_output_usb(const Color& color, const Rumble& rumble) {
    std::vector<uint8_t> report(32, 0);
    report[0] = 0x05;      // Report ID
    report[1] = 0xFF;      // Enable flags: rumble + LED
    report[4] = rumble.right_motor;
    report[5] = rumble.left_motor;
    report[6] = color.r;
    report[7] = color.g;
    report[8] = color.b;
    return report;
}

std::vector<uint8_t> PlayStationDevice::create_ds4_output_bt(const Color& color, const Rumble& rumble) {
    std::vector<uint8_t> report(78, 0);
    report[0] = 0x11;      // BT output report ID
    report[1] = 0x80;
    report[3] = 0xFF;      // Enable flags
    report[6] = rumble.right_motor;
    report[7] = rumble.left_motor;
    report[8] = color.r;
    report[9] = color.g;
    report[10] = color.b;
    return report;
}

std::vector<uint8_t> PlayStationDevice::create_dualsense_output_usb(const Color& color, const Rumble& rumble) {
    // DualSense USB output report: Report ID 0x02, 48 bytes
    // Byte  0: Report ID (0x02)
    // Byte  1: Valid flags 0 — bit0: compatible rumble, bit1: haptics, bit2: R2 trigger, bit3: L2 trigger
    // Byte  2: Valid flags 1 — bit0: mic LED, bit2: lightbar, bit4: player LEDs
    // Byte  3: Right haptic motor
    // Byte  4: Left haptic motor
    // Bytes 11-21: Right trigger effect
    // Bytes 22-32: Left trigger effect
    // Byte  39: Player LED brightness (0=high, 1=mid, 2=low)
    // Byte  44: Player LED bitmask (5 bits)
    // Bytes 45-47: Lightbar R, G, B
    std::vector<uint8_t> report(48, 0);
    report[0] = 0x02;
    report[1] = 0x01 | 0x02  // Enable compatible rumble + haptics
              | 0x04 | 0x08; // Enable R2 + L2 trigger effects
    report[2] = 0x04          // Enable lightbar
              | 0x10;         // Enable player LEDs
    report[3] = rumble.right_motor;
    report[4] = rumble.left_motor;

    // Right trigger effect (bytes 11-17)
    report[11] = static_cast<uint8_t>(last_right_trigger_effect_.mode);
    report[12] = last_right_trigger_effect_.start;
    report[13] = last_right_trigger_effect_.end;
    report[14] = last_right_trigger_effect_.force;
    report[15] = last_right_trigger_effect_.param1;
    report[16] = last_right_trigger_effect_.param2;

    // Left trigger effect (bytes 22-28)
    report[22] = static_cast<uint8_t>(last_left_trigger_effect_.mode);
    report[23] = last_left_trigger_effect_.start;
    report[24] = last_left_trigger_effect_.end;
    report[25] = last_left_trigger_effect_.force;
    report[26] = last_left_trigger_effect_.param1;
    report[27] = last_left_trigger_effect_.param2;

    // Player LEDs
    report[39] = 0x00; // Brightness: high
    report[44] = last_player_leds_;

    // Lightbar color
    report[45] = color.r;
    report[46] = color.g;
    report[47] = color.b;
    return report;
}

std::vector<uint8_t> PlayStationDevice::create_dualsense_output_bt(const Color& color, const Rumble& rumble) {
    // DualSense BT output report: Report ID 0x31, 78 bytes
    // Same layout as USB but shifted +1 (byte 1 = BT sequence tag, data starts at byte 2)
    std::vector<uint8_t> report(78, 0);
    report[0] = 0x31;
    report[1] = 0x02;      // BT sequence tag
    report[2] = 0x01 | 0x02 | 0x04 | 0x08; // Valid flags 0
    report[3] = 0x04 | 0x10;                // Valid flags 1
    report[4] = rumble.right_motor;
    report[5] = rumble.left_motor;

    // Right trigger effect (bytes 12-18, offset +1 from USB)
    report[12] = static_cast<uint8_t>(last_right_trigger_effect_.mode);
    report[13] = last_right_trigger_effect_.start;
    report[14] = last_right_trigger_effect_.end;
    report[15] = last_right_trigger_effect_.force;
    report[16] = last_right_trigger_effect_.param1;
    report[17] = last_right_trigger_effect_.param2;

    // Left trigger effect (bytes 23-29, offset +1 from USB)
    report[23] = static_cast<uint8_t>(last_left_trigger_effect_.mode);
    report[24] = last_left_trigger_effect_.start;
    report[25] = last_left_trigger_effect_.end;
    report[26] = last_left_trigger_effect_.force;
    report[27] = last_left_trigger_effect_.param1;
    report[28] = last_left_trigger_effect_.param2;

    // Player LEDs
    report[40] = 0x00; // Brightness: high
    report[45] = last_player_leds_;

    // Lightbar color
    report[46] = color.r;
    report[47] = color.g;
    report[48] = color.b;
    return report;
}

// --- Device info / factory ---

std::vector<PlayStationDeviceInfo> getPlayStationDeviceInfos() {
    return {
        {0x054C, 0x05C4, "DualShock 4 v1"},
        {0x054C, 0x09CC, "DualShock 4 v2"},
        {0x054C, 0x0CE6, "DualSense"},
        {0x054C, 0x0DF2, "DualSense Edge"},
    };
}

std::unique_ptr<GamepadDevice> createPlayStationDevice(std::unique_ptr<HidDevice> hid_device, int index) {
    return std::make_unique<PlayStationDevice>(std::move(hid_device), index);
}

} // namespace internal
} // namespace gcpad
