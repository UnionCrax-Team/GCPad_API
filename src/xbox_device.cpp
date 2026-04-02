#include "xbox_device.h"
#include "hid_device.h"
#include <iostream>
#include <cstring>

namespace gcpad {
namespace internal {

class XboxDevice : public GamepadDevice {
public:
    XboxDevice(std::unique_ptr<HidDevice> hid_device, int index);
    ~XboxDevice() override;

    int getIndex() const override { return index_; }
    std::string getName() const override { return "Xbox Controller"; }
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
    GamepadState state_;
    std::shared_ptr<Remapper> remapper_;

    static constexpr uint16_t MICROSOFT_VID = 0x045E;
    static constexpr uint16_t XBOX_ONE_PID = 0x028E;
    static constexpr uint16_t XBOX_SERIES_PID = 0x0B05;

    bool parse_input_report(const std::vector<uint8_t>& report);
};

XboxDevice::XboxDevice(std::unique_ptr<HidDevice> hid_device, int index)
    : hid_device_(std::move(hid_device)), index_(index), connected_(false), remapper_(nullptr) {
    state_.reset();
}

XboxDevice::~XboxDevice() {
    if (connected_) hid_device_->close();
}

std::string XboxDevice::getSerialNumber() const {
    if (!connected_) return "";
    return hid_device_->get_serial_number_string();
}

GamepadState XboxDevice::getRemappedState() const {
    if (remapper_) return remapper_->apply(state_);
    return state_;
}

void XboxDevice::setRemapper(std::shared_ptr<Remapper> remapper) {
    remapper_ = std::move(remapper);
}

bool XboxDevice::updateState() {
    if (!connected_) {
        if (!hid_device_->open()) return false;
        connected_ = true;
        state_.is_connected = true;
    }

    std::vector<uint8_t> report;
    if (!hid_device_->read(report)) {
        connected_ = false;
        state_.is_connected = false;
        return false;
    }

    return parse_input_report(report);
}

bool XboxDevice::parse_input_report(const std::vector<uint8_t>& report) {
    if (report.size() < 20) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());

    uint8_t buttons1 = report[4];
    uint8_t buttons2 = report[5];

    state_.buttons[static_cast<size_t>(Button::A)] = (buttons1 & 0x10) != 0;
    state_.buttons[static_cast<size_t>(Button::B)] = (buttons1 & 0x20) != 0;
    state_.buttons[static_cast<size_t>(Button::X)] = (buttons1 & 0x40) != 0;
    state_.buttons[static_cast<size_t>(Button::Y)] = (buttons1 & 0x80) != 0;

    state_.buttons[static_cast<size_t>(Button::L1)] = (buttons1 & 0x01) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)] = (buttons1 & 0x02) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] = (buttons1 & 0x04) != 0;
    state_.buttons[static_cast<size_t>(Button::Start)] = (buttons1 & 0x08) != 0;

    // Dpad
    uint8_t dpad = buttons2 & 0x0F;
    state_.buttons[static_cast<size_t>(Button::DPad_Up)] = (dpad == 0 || dpad == 1 || dpad == 7);
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (dpad >= 1 && dpad <= 3);
    state_.buttons[static_cast<size_t>(Button::DPad_Down)] = (dpad >= 3 && dpad <= 5);
    state_.buttons[static_cast<size_t>(Button::DPad_Left)] = (dpad >= 5 && dpad <= 7);

    int16_t lx = static_cast<int16_t>((report[6] | (report[7] << 8)));
    int16_t ly = static_cast<int16_t>((report[8] | (report[9] << 8)));
    int16_t rx = static_cast<int16_t>((report[10] | (report[11] << 8)));
    int16_t ry = static_cast<int16_t>((report[12] | (report[13] << 8)));

    state_.axes[static_cast<size_t>(Axis::LeftX)] = lx / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::LeftY)] = ly / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = rx / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = ry / 32767.0f;

    state_.axes[static_cast<size_t>(Axis::LeftTrigger)] = report[2] / 255.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = report[3] / 255.0f;

    return true;
}

bool XboxDevice::setRumble(const Rumble& rumble) {
    // On Xbox with HID this is handled with output report 0x03 if supported.
    std::vector<uint8_t> report(8, 0);
    report[0] = 0x03;
    report[2] = rumble.left_motor;
    report[3] = rumble.right_motor;
    return hid_device_->write(report);
}

std::vector<XboxDeviceInfo> getXboxDeviceInfos() {
    return {
        {0x045E, 0x028E, "Xbox One Controller"},
        {0x045E, 0x0B05, "Xbox Series X|S Controller"},
    };
}

std::unique_ptr<GamepadDevice> createXboxDevice(std::unique_ptr<HidDevice> hid_device, int index) {
    return std::make_unique<XboxDevice>(std::move(hid_device), index);
}

} // namespace internal
} // namespace gcpad