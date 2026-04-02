#include "nintendo_device.h"
#include "hid_device.h"
#include <iostream>
#include <cstring>

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
    GamepadState state_;
    std::shared_ptr<Remapper> remapper_;

    static constexpr uint16_t NINTENDO_VID = 0x057E;
    static constexpr uint16_t SWITCH_PRO_PID = 0x2009;
    static constexpr uint16_t JOYCON_L_PID = 0x2006;
    static constexpr uint16_t JOYCON_R_PID = 0x2007;

    bool parse_input_report(const std::vector<uint8_t>& report);
};

NintendoDevice::NintendoDevice(std::unique_ptr<HidDevice> hid_device, int index)
    : hid_device_(std::move(hid_device)), index_(index), connected_(false), remapper_(nullptr) {
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

bool NintendoDevice::updateState() {
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

bool NintendoDevice::parse_input_report(const std::vector<uint8_t>& report) {
    if (report.size() < 18) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());

    // Simplified Switch Pro report decoding (USB full report)
    // Left stick
    int16_t lx = static_cast<int16_t>((report[6] | ((report[7] & 0x0F) << 8)));
    int16_t ly = static_cast<int16_t>(((report[7] >> 4) | (report[8] << 4)));
    int16_t rx = static_cast<int16_t>(((report[9] & 0x3F) | (report[10] << 6)));
    int16_t ry = static_cast<int16_t>(((report[10] >> 2) | (report[11] << 6)));

    state_.axes[static_cast<size_t>(Axis::LeftX)] = (lx - 2048) / 2048.0f;
    state_.axes[static_cast<size_t>(Axis::LeftY)] = (ly - 2048) / 2048.0f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = (rx - 2048) / 2048.0f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = (ry - 2048) / 2048.0f;

    uint16_t buttons = report[3] | (report[4] << 8);
    state_.buttons[static_cast<size_t>(Button::A)] = (buttons & (1 << 0)) != 0;
    state_.buttons[static_cast<size_t>(Button::B)] = (buttons & (1 << 1)) != 0;
    state_.buttons[static_cast<size_t>(Button::X)] = (buttons & (1 << 2)) != 0;
    state_.buttons[static_cast<size_t>(Button::Y)] = (buttons & (1 << 3)) != 0;
    state_.buttons[static_cast<size_t>(Button::L1)] = (buttons & (1 << 4)) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)] = (buttons & (1 << 5)) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] = (buttons & (1 << 8)) != 0;
    state_.buttons[static_cast<size_t>(Button::Start)] = (buttons & (1 << 9)) != 0;

    state_.axes[static_cast<size_t>(Axis::LeftTrigger)] = ((buttons & (1 << 10)) != 0) ? 1.0f : 0.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = ((buttons & (1 << 11)) != 0) ? 1.0f : 0.0f;

    return true;
}

bool NintendoDevice::setRumble(const Rumble& rumble) {
    // Simplified rumble packet (for Pro controller protocol)
    std::vector<uint8_t> report(8, 0);
    report[0] = 0x10;
    report[1] = rumble.left_motor;
    report[2] = rumble.right_motor;
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