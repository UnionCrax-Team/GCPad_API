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
    // Xbox One/Series controllers over USB HID use a specific GIP-over-HID report format:
    // Report ID 0x01, minimum 16 bytes (varies by firmware)
    //
    // Byte layout (GIP-over-HID):
    //   [0] = Report ID (0x01)
    //   [1] = LX low byte
    //   [2] = LX high byte
    //   [3] = LY low byte
    //   [4] = LY high byte
    //   [5] = RX low byte
    //   [6] = RX high byte
    //   [7] = RY low byte
    //   [8] = RY high byte
    //   [9] = LT (0-1023, or 0-255 depending on report mode)
    //   [10] = LT high / RT low
    //   [11] = RT (high bits)
    //   [12] = buttons1 (ABXY, bumpers)
    //   [13] = buttons2 (hat + misc)
    //   [14] = Guide (bit 0), share (bit 0 on newer fw)
    //
    // However, many Xbox controllers use a simpler 16-byte format:
    //   [0] = Report ID
    //   [1] = ?? (usually 0x00)
    //   [2] = LT (0-255)
    //   [3] = RT (0-255)
    //   [4] = buttons1
    //   [5] = buttons2 (hat)
    //   [6-7] = LX (int16)
    //   [8-9] = LY (int16)
    //   [10-11] = RX (int16)
    //   [12-13] = RY (int16)
    //
    // We accept both formats by checking report size and using the simpler, more common one.

    if (report.size() < 16) return false;

    state_.setTimestamp(std::chrono::steady_clock::now());
    state_.is_connected = true;

    // Common Xbox HID format (16-17 bytes)
    uint8_t buttons1 = report[4];
    uint8_t buttons2 = report[5];

    // Face buttons
    state_.buttons[static_cast<size_t>(Button::A)] = (buttons1 & 0x10) != 0;
    state_.buttons[static_cast<size_t>(Button::B)] = (buttons1 & 0x20) != 0;
    state_.buttons[static_cast<size_t>(Button::X)] = (buttons1 & 0x40) != 0;
    state_.buttons[static_cast<size_t>(Button::Y)] = (buttons1 & 0x80) != 0;

    // Bumpers and options
    state_.buttons[static_cast<size_t>(Button::L1)]     = (buttons1 & 0x01) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)]     = (buttons1 & 0x02) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)]  = (buttons1 & 0x04) != 0; // View/Back
    state_.buttons[static_cast<size_t>(Button::Start)]   = (buttons1 & 0x08) != 0; // Menu

    // D-Pad (hat switch)
    uint8_t dpad = buttons2 & 0x0F;
    state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = (dpad == 1 || dpad == 2 || dpad == 8);
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (dpad == 2 || dpad == 3 || dpad == 4);
    state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = (dpad == 4 || dpad == 5 || dpad == 6);
    state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = (dpad == 6 || dpad == 7 || dpad == 8);

    // Thumbstick clicks
    state_.buttons[static_cast<size_t>(Button::L3)] = (buttons2 & 0x40) != 0;
    state_.buttons[static_cast<size_t>(Button::R3)] = (buttons2 & 0x80) != 0;

    // Sticks (16-bit signed, little-endian)
    int16_t lx = static_cast<int16_t>(report[6]  | (report[7]  << 8));
    int16_t ly = static_cast<int16_t>(report[8]  | (report[9]  << 8));
    int16_t rx = static_cast<int16_t>(report[10] | (report[11] << 8));
    int16_t ry = static_cast<int16_t>(report[12] | (report[13] << 8));

    state_.axes[static_cast<size_t>(Axis::LeftX)]  = lx / 32768.0f;
    state_.axes[static_cast<size_t>(Axis::LeftY)]  = ly / 32768.0f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = rx / 32768.0f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = ry / 32768.0f;

    // Triggers (8-bit, at bytes 2-3)
    state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = report[2] / 255.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = report[3] / 255.0f;

    // Digital trigger flags
    state_.buttons[static_cast<size_t>(Button::L2)] = (report[2] > 128);
    state_.buttons[static_cast<size_t>(Button::R2)] = (report[3] > 128);

    // Guide button (if present in extended reports)
    if (report.size() >= 15) {
        state_.buttons[static_cast<size_t>(Button::Guide)] = (report[14] & 0x01) != 0;
    }

    return true;
}

bool XboxDevice::setRumble(const Rumble& rumble) {
    if (!connected_) return false;

    // Xbox HID rumble output report
    std::vector<uint8_t> report(8, 0);
    report[0] = 0x03;  // Report ID
    report[1] = 0x03;  // Enable left + right motor
    report[2] = rumble.left_motor;
    report[3] = rumble.right_motor;
    return hid_device_->write(report);
}

std::vector<XboxDeviceInfo> getXboxDeviceInfos() {
    return {
        // Xbox One controllers
        {0x045E, 0x02D1, "Xbox One Controller"},
        {0x045E, 0x02DD, "Xbox One Controller (FW 2015)"},
        {0x045E, 0x02E3, "Xbox One Elite Controller"},
        {0x045E, 0x02EA, "Xbox One S Controller"},
        {0x045E, 0x028E, "Xbox 360 Controller"},
        // Xbox Series X|S controllers
        {0x045E, 0x0B12, "Xbox Series X|S Controller"},
        {0x045E, 0x0B13, "Xbox Series X|S Controller (BT)"},
        {0x045E, 0x0B05, "Xbox One Controller (GIP)"},
        // Xbox Elite Series 2
        {0x045E, 0x0B00, "Xbox Elite Series 2"},
        {0x045E, 0x0B22, "Xbox Adaptive Controller"},
    };
}

std::unique_ptr<GamepadDevice> createXboxDevice(std::unique_ptr<HidDevice> hid_device, int index) {
    return std::make_unique<XboxDevice>(std::move(hid_device), index);
}

} // namespace internal
} // namespace gcpad
