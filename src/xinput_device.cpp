#include "xinput_device.h"
#include <windows.h>
#include <Xinput.h>
#include <cstring>
#include <string>

// XInput is linked via CMake (find_library in CMakeLists.txt)
// #pragma comment(lib, "xinput1_4.lib")  -- REMOVED to avoid linking wrong architecture

namespace gcpad {
namespace internal {

class XInputDevice : public GamepadDevice {
public:
    XInputDevice(int xinput_index, int slot);
    ~XInputDevice() override = default;

    int getIndex() const override { return slot_; }
    std::string getName() const override { return "Xbox Controller (XInput " + std::to_string(xinput_index_) + ")"; }
    std::string getSerialNumber() const override { return ""; } // XInput doesn't provide serial

    const GamepadState& getState() const override { return state_; }
    GamepadState getRemappedState() const override;
    bool updateState() override;

    bool setLED(const Color& /*color*/) override { return false; }
    bool setRumble(const Rumble& rumble) override;

    void setRemapper(std::shared_ptr<Remapper> remapper) override;
    bool isConnected() const override { return connected_; }

private:
    int xinput_index_;
    int slot_;
    bool connected_;
    GamepadState state_;
    std::shared_ptr<Remapper> remapper_;
};

XInputDevice::XInputDevice(int xinput_index, int slot)
    : xinput_index_(xinput_index), slot_(slot), connected_(false), remapper_(nullptr) {
    state_.reset();
}

GamepadState XInputDevice::getRemappedState() const {
    if (remapper_) return remapper_->apply(state_);
    return state_;
}

void XInputDevice::setRemapper(std::shared_ptr<Remapper> remapper) {
    remapper_ = std::move(remapper);
}

bool XInputDevice::updateState() {
    XINPUT_STATE xstate;
    memset(&xstate, 0, sizeof(xstate));

    DWORD result = XInputGetState(static_cast<DWORD>(xinput_index_), &xstate);

    if (result != ERROR_SUCCESS) {
        if (connected_) {
            connected_ = false;
            state_.is_connected = false;
        }
        return false;
    }

    connected_ = true;
    state_.is_connected = true;
    state_.setTimestamp(std::chrono::steady_clock::now());

    const XINPUT_GAMEPAD& gp = xstate.Gamepad;

    // Buttons
    state_.buttons[static_cast<size_t>(Button::A)]      = (gp.wButtons & XINPUT_GAMEPAD_A) != 0;
    state_.buttons[static_cast<size_t>(Button::B)]      = (gp.wButtons & XINPUT_GAMEPAD_B) != 0;
    state_.buttons[static_cast<size_t>(Button::X)]      = (gp.wButtons & XINPUT_GAMEPAD_X) != 0;
    state_.buttons[static_cast<size_t>(Button::Y)]      = (gp.wButtons & XINPUT_GAMEPAD_Y) != 0;
    state_.buttons[static_cast<size_t>(Button::L1)]     = (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)]     = (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] = (gp.wButtons & XINPUT_GAMEPAD_BACK) != 0;
    state_.buttons[static_cast<size_t>(Button::Start)]  = (gp.wButtons & XINPUT_GAMEPAD_START) != 0;
    state_.buttons[static_cast<size_t>(Button::L3)]     = (gp.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    state_.buttons[static_cast<size_t>(Button::R3)]     = (gp.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;

    state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    // Triggers (0-255)
    state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = gp.bLeftTrigger / 255.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] = gp.bRightTrigger / 255.0f;
    state_.buttons[static_cast<size_t>(Button::L2)] = (gp.bLeftTrigger > 128);
    state_.buttons[static_cast<size_t>(Button::R2)] = (gp.bRightTrigger > 128);

    // Sticks (-32768 to 32767)
    state_.axes[static_cast<size_t>(Axis::LeftX)]  = gp.sThumbLX / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::LeftY)]  = gp.sThumbLY / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightX)] = gp.sThumbRX / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightY)] = gp.sThumbRY / 32767.0f;

    // Guide button (requires XInputGetState with hidden ordinal #100 on some SDK versions;
    // standard XInput doesn't expose it, so we skip it)
    state_.buttons[static_cast<size_t>(Button::Guide)] = false;

    return true;
}

bool XInputDevice::setRumble(const Rumble& rumble) {
    XINPUT_VIBRATION vib;
    vib.wLeftMotorSpeed = static_cast<WORD>(rumble.left_motor) * 257; // Scale 0-255 to 0-65535
    vib.wRightMotorSpeed = static_cast<WORD>(rumble.right_motor) * 257;
    return XInputSetState(static_cast<DWORD>(xinput_index_), &vib) == ERROR_SUCCESS;
}

std::unique_ptr<GamepadDevice> createXInputDevice(int xinput_index, int slot) {
    return std::make_unique<XInputDevice>(xinput_index, slot);
}

std::vector<int> getConnectedXInputIndices() {
    std::vector<int> indices;
    for (int i = 0; i < 4; ++i) {
        XINPUT_STATE state;
        memset(&state, 0, sizeof(state));
        if (XInputGetState(static_cast<DWORD>(i), &state) == ERROR_SUCCESS) {
            indices.push_back(i);
        }
    }
    return indices;
}

} // namespace internal
} // namespace gcpad
