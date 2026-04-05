#include "ps_device.h"
#include "hid_device.h"
#include <iostream>
#include <cstring>

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
        connected_ = false;
        state_.is_connected = false;
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
    // We distinguish USB vs BT by the input report length:
    //   DS4 USB:  64 bytes    DS4 BT:  547 bytes (or report ID 0x11, ~78 bytes)
    //   DS5 USB:  64 bytes    DS5 BT:  78 bytes (report ID 0x31)
    // As a heuristic: if input report > 64 bytes, it's likely Bluetooth.
    if (caps.input_report_byte_length > 64) {
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

    // PS button + touchpad click
    state_.buttons[static_cast<size_t>(Button::Guide)] = (data[6] & 0x01) != 0;

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

    // IMU data (gyro + accel) at bytes 13-24
    if (report.size() >= 25) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[14] << 8) | report[13]));
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[16] << 8) | report[15]));
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[18] << 8) | report[17]));
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[20] << 8) | report[19]));
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[22] << 8) | report[21]));
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[24] << 8) | report[23]));
    }

    // Battery
    if (report.size() >= 31) {
        uint8_t battery_raw = report[30];
        state_.battery_level = (battery_raw & 0x0F) / 10.0f;
        state_.is_charging = (battery_raw & 0x10) != 0;
    }

    return true;
}

bool PlayStationDevice::parse_ds4_bt(const std::vector<uint8_t>& report) {
    // DS4 Bluetooth: Report ID 0x11, 78+ bytes
    // The data layout is the same as USB but shifted by +2 bytes:
    // Byte 0 = report ID (0x11)
    // Byte 1-2 = protocol header
    // Byte 3 = LX, 4 = LY, 5 = RX, 6 = RY
    // Byte 7 = buttons1, 8 = buttons2, 9 = PS/TP counter
    // Byte 10 = L2, 11 = R2
    if (report.size() < 32) return false;
    if (report[0] != 0x11) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    // Button/axis data starts at byte 3 (offset +2 from USB)
    parse_ds4_buttons(&report[3]);

    // IMU at offset +2: bytes 15-26
    if (report.size() >= 27) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[16] << 8) | report[15]));
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[18] << 8) | report[17]));
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[20] << 8) | report[19]));
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[22] << 8) | report[21]));
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[24] << 8) | report[23]));
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[26] << 8) | report[25]));
    }

    // Battery at offset +2: byte 32
    if (report.size() >= 33) {
        uint8_t battery_raw = report[32];
        state_.battery_level = (battery_raw & 0x0F) / 10.0f;
        state_.is_charging = (battery_raw & 0x10) != 0;
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

    // PS button (byte 9, bit 0), Mute (bit 2)
    state_.buttons[static_cast<size_t>(Button::Guide)] = (data[9] & 0x01) != 0;
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

    // Gyro/Accel at bytes 15-26
    if (report.size() >= 27) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[16] << 8) | report[15]));
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[18] << 8) | report[17]));
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[20] << 8) | report[19]));
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[22] << 8) | report[21]));
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[24] << 8) | report[23]));
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[26] << 8) | report[25]));
    }

    // Battery at byte 53
    if (report.size() >= 54) {
        uint8_t battery_raw = report[53];
        uint8_t battery_level = battery_raw & 0x0F;
        uint8_t battery_status = (battery_raw >> 4) & 0x0F;
        state_.battery_level = battery_level / 10.0f;
        state_.is_charging = (battery_status == 1 || battery_status == 2);
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

    // Gyro/Accel at offset +1 from USB positions
    if (report.size() >= 28) {
        state_.gyro.x  = static_cast<float>(static_cast<int16_t>((report[17] << 8) | report[16]));
        state_.gyro.y  = static_cast<float>(static_cast<int16_t>((report[19] << 8) | report[18]));
        state_.gyro.z  = static_cast<float>(static_cast<int16_t>((report[21] << 8) | report[20]));
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[23] << 8) | report[22]));
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[25] << 8) | report[24]));
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[27] << 8) | report[26]));
    }

    // Battery at byte 54
    if (report.size() >= 55) {
        uint8_t battery_raw = report[54];
        uint8_t battery_level = battery_raw & 0x0F;
        uint8_t battery_status = (battery_raw >> 4) & 0x0F;
        state_.battery_level = battery_level / 10.0f;
        state_.is_charging = (battery_status == 1 || battery_status == 2);
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
    std::vector<uint8_t> report(48, 0);
    report[0] = 0x02;      // Report ID
    report[1] = 0xFF;      // Valid flags byte 0 (enable rumble + haptics)
    report[2] = 0x01 | 0x02 | 0x04; // Valid flags byte 1 (lightbar + player LEDs)
    report[3] = rumble.right_motor;  // Right haptic
    report[4] = rumble.left_motor;   // Left haptic
    // Lightbar color at bytes 45-47
    report[45] = color.r;
    report[46] = color.g;
    report[47] = color.b;
    return report;
}

std::vector<uint8_t> PlayStationDevice::create_dualsense_output_bt(const Color& color, const Rumble& rumble) {
    // DualSense BT output report: Report ID 0x31, 78 bytes
    std::vector<uint8_t> report(78, 0);
    report[0] = 0x31;
    report[1] = 0x02;      // BT sequence tag
    report[2] = 0xFF;      // Valid flags byte 0
    report[3] = 0x01 | 0x02 | 0x04; // Valid flags byte 1
    report[4] = rumble.right_motor;
    report[5] = rumble.left_motor;
    // Lightbar color at bytes 46-48
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
