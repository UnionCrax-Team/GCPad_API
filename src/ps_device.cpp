#include "ps_device.h"
#include "hid_device.h"
#include <iostream>
#include <cstring>

namespace gcpad {
namespace internal {

// PlayStation device class - handles DS4, DS5 and similar
class PlayStationDevice : public GamepadDevice {
public:
    PlayStationDevice(std::unique_ptr<HidDevice> hid_device, int index);
    ~PlayStationDevice() override;

    // GamepadDevice interface
    int getIndex() const override { return index_; }
    std::string getName() const override { return "PlayStation Controller"; }
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
    ConnectionType connection_type_;
    std::shared_ptr<Remapper> remapper_;

    // Internal methods
    bool detect_connection_type();
    bool parse_input_report(const std::vector<uint8_t>& report);
    std::vector<uint8_t> create_output_report(const Color& color, const Rumble& rumble);

    // PS constants
    static constexpr uint16_t SONY_VID = 0x054C;
    static constexpr uint16_t DS4_USB_PID = 0x05C4;
    static constexpr uint16_t DS4_BT_PID = 0x09CC;
    static constexpr uint16_t DS5_USB_PID = 0x0CE6;
    static constexpr uint16_t DS5_BT_PID = 0x0CC4;
};

PlayStationDevice::PlayStationDevice(std::unique_ptr<HidDevice> hid_device, int index)
    : hid_device_(std::move(hid_device)), index_(index), connected_(false), connection_type_(ConnectionType::USB), remapper_(nullptr) {
    state_.reset();
}

PlayStationDevice::~PlayStationDevice() {
    if (connected_) {
        hid_device_->close();
    }
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
        if (hid_device_->open() && detect_connection_type()) {
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

    return parse_input_report(report);
}

bool PlayStationDevice::setLED(const Color& color) {
    if (!connected_) return false;

    auto report = create_output_report(color, Rumble{});
    return hid_device_->write(report);
}

bool PlayStationDevice::setRumble(const Rumble& rumble) {
    if (!connected_) return false;

    auto report = create_output_report(Color{}, rumble);
    return hid_device_->write(report);
}

bool PlayStationDevice::detect_connection_type() {
    auto attributes = hid_device_->get_attributes();

    if (attributes.product_id == DS4_USB_PID || attributes.product_id == DS5_USB_PID) {
        connection_type_ = ConnectionType::USB;
        return true;
    } else if (attributes.product_id == DS4_BT_PID || attributes.product_id == DS5_BT_PID) {
        connection_type_ = ConnectionType::Bluetooth;
        return true;
    }

    return false;
}

bool PlayStationDevice::parse_input_report(const std::vector<uint8_t>& report) {
    if (report.size() < 64) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    if (connection_type_ == ConnectionType::USB) {
        if (report[0] != 0x01) return false;
        uint8_t buttons1 = report[5];
        uint8_t buttons2 = report[6];

        state_.buttons[static_cast<size_t>(Button::A)] = (buttons1 & 0x20) != 0; // Cross -> A
        state_.buttons[static_cast<size_t>(Button::B)] = (buttons1 & 0x40) != 0; // Circle -> B
        state_.buttons[static_cast<size_t>(Button::X)] = (buttons1 & 0x10) != 0; // Square -> X
        state_.buttons[static_cast<size_t>(Button::Y)] = (buttons1 & 0x80) != 0; // Triangle -> Y

        state_.buttons[static_cast<size_t>(Button::L1)] = (buttons2 & 0x01) != 0;
        state_.buttons[static_cast<size_t>(Button::R1)] = (buttons2 & 0x02) != 0;
        state_.buttons[static_cast<size_t>(Button::L2)] = (buttons2 & 0x04) != 0;
        state_.buttons[static_cast<size_t>(Button::R2)] = (buttons2 & 0x08) != 0;
        state_.buttons[static_cast<size_t>(Button::Select)] = (buttons2 & 0x10) != 0;
        state_.buttons[static_cast<size_t>(Button::Start)] = (buttons2 & 0x20) != 0;
        state_.buttons[static_cast<size_t>(Button::Guide)] = (report[7] & 0x01) != 0;
        state_.buttons[static_cast<size_t>(Button::L3)] = (buttons2 & 0x40) != 0;
        state_.buttons[static_cast<size_t>(Button::R3)] = (buttons2 & 0x80) != 0;

        uint8_t dpad = buttons1 & 0x0F;
        state_.buttons[static_cast<size_t>(Button::DPad_Up)] = (dpad == 0 || dpad == 1 || dpad == 7);
        state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (dpad >= 1 && dpad <= 3);
        state_.buttons[static_cast<size_t>(Button::DPad_Down)] = (dpad >= 3 && dpad <= 5);
        state_.buttons[static_cast<size_t>(Button::DPad_Left)] = (dpad >= 5 && dpad <= 7);

        state_.axes[static_cast<size_t>(Axis::LeftX)] = (report[1] - 128.0f) / 127.5f;
        state_.axes[static_cast<size_t>(Axis::LeftY)] = (report[2] - 128.0f) / 127.5f;
        state_.axes[static_cast<size_t>(Axis::RightX)] = (report[3] - 128.0f) / 127.5f;
        state_.axes[static_cast<size_t>(Axis::RightY)] = (report[4] - 128.0f) / 127.5f;

        state_.axes[static_cast<size_t>(Axis::LeftTrigger)] = report[8] / 255.0f;
        state_.axes[static_cast<size_t>(Axis::RightTrigger)] = report[9] / 255.0f;

        state_.gyro.x = static_cast<float>(static_cast<int16_t>((report[14] << 8) | report[13]));
        state_.gyro.y = static_cast<float>(static_cast<int16_t>((report[16] << 8) | report[15]));
        state_.gyro.z = static_cast<float>(static_cast<int16_t>((report[18] << 8) | report[17]));
        state_.accel.x = static_cast<float>(static_cast<int16_t>((report[20] << 8) | report[19]));
        state_.accel.y = static_cast<float>(static_cast<int16_t>((report[22] << 8) | report[21]));
        state_.accel.z = static_cast<float>(static_cast<int16_t>((report[24] << 8) | report[23]));

        state_.battery_level = (report[12] & 0x0F) / 15.0f;
        state_.is_charging = (report[12] & 0x10) != 0;

    } else {
        if (report[0] != 0x11) return false;
        uint8_t buttons1 = report[5];
        uint8_t buttons2 = report[6];

        state_.buttons[static_cast<size_t>(Button::A)] = (buttons1 & 0x20) != 0;
        state_.buttons[static_cast<size_t>(Button::B)] = (buttons1 & 0x40) != 0;
        state_.buttons[static_cast<size_t>(Button::X)] = (buttons1 & 0x10) != 0;
        state_.buttons[static_cast<size_t>(Button::Y)] = (buttons1 & 0x80) != 0;

        state_.buttons[static_cast<size_t>(Button::L1)] = (buttons2 & 0x01) != 0;
        state_.buttons[static_cast<size_t>(Button::R1)] = (buttons2 & 0x02) != 0;
        state_.buttons[static_cast<size_t>(Button::L2)] = (buttons2 & 0x04) != 0;
        state_.buttons[static_cast<size_t>(Button::R2)] = (buttons2 & 0x08) != 0;

        state_.axes[static_cast<size_t>(Axis::LeftX)] = (report[1] - 128.0f) / 127.5f;
        state_.axes[static_cast<size_t>(Axis::LeftY)] = (report[2] - 128.0f) / 127.5f;
        state_.axes[static_cast<size_t>(Axis::RightX)] = (report[3] - 128.0f) / 127.5f;
        state_.axes[static_cast<size_t>(Axis::RightY)] = (report[4] - 128.0f) / 127.5f;

        state_.axes[static_cast<size_t>(Axis::LeftTrigger)] = report[8] / 255.0f;
        state_.axes[static_cast<size_t>(Axis::RightTrigger)] = report[9] / 255.0f;
    }

    return true;
}

std::vector<uint8_t> PlayStationDevice::create_output_report(const Color& color, const Rumble& rumble) {
    std::vector<uint8_t> report;

    if (connection_type_ == ConnectionType::USB) {
        report.resize(32, 0);
        report[0] = 0x05;
        report[1] = 0xFF;
        report[4] = rumble.right_motor;
        report[5] = rumble.left_motor;
        report[6] = color.r;
        report[7] = color.g;
        report[8] = color.b;
    } else {
        report.resize(78, 0);
        report[0] = 0x11;
        report[1] = 0x80;
        report[3] = 0xFF;
        report[6] = rumble.right_motor;
        report[7] = rumble.left_motor;
        report[8] = color.r;
        report[9] = color.g;
        report[10] = color.b;
    }

    return report;
}

std::vector<PlayStationDeviceInfo> getPlayStationDeviceInfos() {
    return {
        {0x054C, 0x05C4, "DualShock 4 USB"},
        {0x054C, 0x09CC, "DualShock 4 Bluetooth"},
        {0x054C, 0x0CE6, "DualSense USB"},
        {0x054C, 0x0CC4, "DualSense Bluetooth"},
    };
}

std::unique_ptr<GamepadDevice> createPlayStationDevice(std::unique_ptr<HidDevice> hid_device, int index) {
    return std::make_unique<PlayStationDevice>(std::move(hid_device), index);
}

} // namespace internal
} // namespace gcpad