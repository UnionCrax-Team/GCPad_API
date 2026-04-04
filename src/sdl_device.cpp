#include "sdl_device.h"

// Only compile SDL support if SDL2 headers are available
#if __has_include("SDL2/SDL.h")
#define GCPAD_HAS_SDL2 1
#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>
#include <SDL2/SDL_joystick.h>
#else
#define GCPAD_HAS_SDL2 0
#endif

#include <iostream>
#include <cstring>

namespace gcpad {
namespace internal {

#if GCPAD_HAS_SDL2

// ── Globals ──────────────────────────────────────────────────────────────────
static bool g_sdl_initialized = false;

// ── SDLDevice implementation ────────────────────────────────────────────────

class SDLDevice : public GamepadDevice {
public:
    SDLDevice(int sdl_joystick_index, int slot);
    ~SDLDevice() override;

    int getIndex() const override { return slot_; }
    std::string getName() const override { return name_; }
    std::string getSerialNumber() const override;

    const GamepadState& getState() const override { return state_; }
    GamepadState getRemappedState() const override;
    bool updateState() override;

    bool setLED(const Color& color) override;
    bool setRumble(const Rumble& rumble) override;

    void setRemapper(std::shared_ptr<Remapper> remapper) override;
    bool isConnected() const override { return connected_; }

private:
    int slot_;
    std::string name_;
    bool connected_;
    bool is_game_controller_;  // true = SDL_GameController, false = raw SDL_Joystick
    GamepadState state_;
    std::shared_ptr<Remapper> remapper_;

    SDL_GameController* controller_;  // non-null if is_game_controller_
    SDL_Joystick* joystick_;          // always non-null when connected
    SDL_JoystickID instance_id_;

    bool updateFromGameController();
    bool updateFromJoystick();
};

SDLDevice::SDLDevice(int sdl_joystick_index, int slot)
    : slot_(slot), name_("SDL Controller"), connected_(false),
      is_game_controller_(false), remapper_(nullptr),
      controller_(nullptr), joystick_(nullptr), instance_id_(-1)
{
    state_.reset();

    if (!g_sdl_initialized) return;

    // Pump events so SDL sees the device
    SDL_PumpEvents();

    // Try GameController first (has built-in mapping database)
    if (SDL_IsGameController(sdl_joystick_index)) {
        controller_ = SDL_GameControllerOpen(sdl_joystick_index);
        if (controller_) {
            joystick_ = SDL_GameControllerGetJoystick(controller_);
            is_game_controller_ = true;
            const char* n = SDL_GameControllerName(controller_);
            name_ = std::string("SDL GameController: ") + (n ? n : "Unknown");
        }
    }

    // Fall back to raw joystick
    if (!joystick_) {
        joystick_ = SDL_JoystickOpen(sdl_joystick_index);
        if (joystick_) {
            is_game_controller_ = false;
            const char* n = SDL_JoystickName(joystick_);
            name_ = std::string("SDL Joystick: ") + (n ? n : "Unknown");
        }
    }

    if (joystick_) {
        instance_id_ = SDL_JoystickInstanceID(joystick_);
        connected_ = true;
        state_.is_connected = true;
    }
}

SDLDevice::~SDLDevice() {
    if (controller_) {
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
        joystick_ = nullptr;  // Owned by controller
    } else if (joystick_) {
        SDL_JoystickClose(joystick_);
        joystick_ = nullptr;
    }
}

std::string SDLDevice::getSerialNumber() const {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (joystick_) {
        const char* serial = SDL_JoystickGetSerial(joystick_);
        if (serial) return serial;
    }
#endif
    return "";
}

GamepadState SDLDevice::getRemappedState() const {
    if (remapper_) return remapper_->apply(state_);
    return state_;
}

void SDLDevice::setRemapper(std::shared_ptr<Remapper> remapper) {
    remapper_ = std::move(remapper);
}

bool SDLDevice::updateState() {
    if (!joystick_) {
        connected_ = false;
        state_.is_connected = false;
        return false;
    }

    // Pump SDL events to update joystick state
    SDL_PumpEvents();

    // Check if still attached
    if (!SDL_JoystickGetAttached(joystick_)) {
        connected_ = false;
        state_.is_connected = false;
        return false;
    }

    connected_ = true;
    state_.is_connected = true;
    state_.setTimestamp(std::chrono::steady_clock::now());

    if (is_game_controller_ && controller_) {
        return updateFromGameController();
    } else {
        return updateFromJoystick();
    }
}

bool SDLDevice::updateFromGameController() {
    // SDL_GameController provides a standardised mapping
    // Buttons
    state_.buttons[static_cast<size_t>(Button::A)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_A) != 0;
    state_.buttons[static_cast<size_t>(Button::B)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_B) != 0;
    state_.buttons[static_cast<size_t>(Button::X)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_X) != 0;
    state_.buttons[static_cast<size_t>(Button::Y)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_Y) != 0;
    state_.buttons[static_cast<size_t>(Button::L1)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
    state_.buttons[static_cast<size_t>(Button::R1)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
    state_.buttons[static_cast<size_t>(Button::Select)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_BACK) != 0;
    state_.buttons[static_cast<size_t>(Button::Start)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_START) != 0;
    state_.buttons[static_cast<size_t>(Button::Guide)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_GUIDE) != 0;
    state_.buttons[static_cast<size_t>(Button::L3)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_LEFTSTICK) != 0;
    state_.buttons[static_cast<size_t>(Button::R3)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_RIGHTSTICK) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Up)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Down)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Left)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
    state_.buttons[static_cast<size_t>(Button::DPad_Right)] =
        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;

    // Axes (SDL range: -32768 to 32767)
    state_.axes[static_cast<size_t>(Axis::LeftX)] =
        SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::LeftY)] =
        SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightX)] =
        SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightY)] =
        SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;

    // Triggers (SDL range: 0 to 32767)
    state_.axes[static_cast<size_t>(Axis::LeftTrigger)] =
        SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    state_.axes[static_cast<size_t>(Axis::RightTrigger)] =
        SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;

    state_.buttons[static_cast<size_t>(Button::L2)] =
        state_.axes[static_cast<size_t>(Axis::LeftTrigger)] > 0.5f;
    state_.buttons[static_cast<size_t>(Button::R2)] =
        state_.axes[static_cast<size_t>(Axis::RightTrigger)] > 0.5f;

    // Battery (SDL 2.0.4+)
#if SDL_VERSION_ATLEAST(2, 0, 4)
    SDL_JoystickPowerLevel power = SDL_JoystickCurrentPowerLevel(joystick_);
    switch (power) {
        case SDL_JOYSTICK_POWER_EMPTY:  state_.battery_level = 0.05f; break;
        case SDL_JOYSTICK_POWER_LOW:    state_.battery_level = 0.20f; break;
        case SDL_JOYSTICK_POWER_MEDIUM: state_.battery_level = 0.60f; break;
        case SDL_JOYSTICK_POWER_FULL:   state_.battery_level = 1.00f; break;
        case SDL_JOYSTICK_POWER_WIRED:  state_.battery_level = 1.00f; state_.is_charging = true; break;
        default:                         state_.battery_level = 0.0f; break;
    }
#endif

    return true;
}

bool SDLDevice::updateFromJoystick() {
    // Raw joystick — axes and buttons by index
    int numAxes = SDL_JoystickNumAxes(joystick_);
    int numButtons = SDL_JoystickNumButtons(joystick_);
    int numHats = SDL_JoystickNumHats(joystick_);

    // Axes (up to 6)
    if (numAxes >= 1) state_.axes[static_cast<size_t>(Axis::LeftX)]  = SDL_JoystickGetAxis(joystick_, 0) / 32767.0f;
    if (numAxes >= 2) state_.axes[static_cast<size_t>(Axis::LeftY)]  = SDL_JoystickGetAxis(joystick_, 1) / 32767.0f;
    if (numAxes >= 3) state_.axes[static_cast<size_t>(Axis::RightX)] = SDL_JoystickGetAxis(joystick_, 2) / 32767.0f;
    if (numAxes >= 4) state_.axes[static_cast<size_t>(Axis::RightY)] = SDL_JoystickGetAxis(joystick_, 3) / 32767.0f;
    if (numAxes >= 5) state_.axes[static_cast<size_t>(Axis::LeftTrigger)]  = (SDL_JoystickGetAxis(joystick_, 4) + 32768.0f) / 65535.0f;
    if (numAxes >= 6) state_.axes[static_cast<size_t>(Axis::RightTrigger)] = (SDL_JoystickGetAxis(joystick_, 5) + 32768.0f) / 65535.0f;

    // Buttons (map first 11 to standard layout)
    if (numButtons >= 1)  state_.buttons[static_cast<size_t>(Button::A)]      = SDL_JoystickGetButton(joystick_, 0) != 0;
    if (numButtons >= 2)  state_.buttons[static_cast<size_t>(Button::B)]      = SDL_JoystickGetButton(joystick_, 1) != 0;
    if (numButtons >= 3)  state_.buttons[static_cast<size_t>(Button::X)]      = SDL_JoystickGetButton(joystick_, 2) != 0;
    if (numButtons >= 4)  state_.buttons[static_cast<size_t>(Button::Y)]      = SDL_JoystickGetButton(joystick_, 3) != 0;
    if (numButtons >= 5)  state_.buttons[static_cast<size_t>(Button::L1)]     = SDL_JoystickGetButton(joystick_, 4) != 0;
    if (numButtons >= 6)  state_.buttons[static_cast<size_t>(Button::R1)]     = SDL_JoystickGetButton(joystick_, 5) != 0;
    if (numButtons >= 7)  state_.buttons[static_cast<size_t>(Button::Select)] = SDL_JoystickGetButton(joystick_, 6) != 0;
    if (numButtons >= 8)  state_.buttons[static_cast<size_t>(Button::Start)]  = SDL_JoystickGetButton(joystick_, 7) != 0;
    if (numButtons >= 9)  state_.buttons[static_cast<size_t>(Button::L3)]     = SDL_JoystickGetButton(joystick_, 8) != 0;
    if (numButtons >= 10) state_.buttons[static_cast<size_t>(Button::R3)]     = SDL_JoystickGetButton(joystick_, 9) != 0;
    if (numButtons >= 11) state_.buttons[static_cast<size_t>(Button::Guide)]  = SDL_JoystickGetButton(joystick_, 10) != 0;

    // L2/R2 digital from trigger axes
    state_.buttons[static_cast<size_t>(Button::L2)] = state_.axes[static_cast<size_t>(Axis::LeftTrigger)] > 0.5f;
    state_.buttons[static_cast<size_t>(Button::R2)] = state_.axes[static_cast<size_t>(Axis::RightTrigger)] > 0.5f;

    // D-Pad from hat
    if (numHats >= 1) {
        Uint8 hat = SDL_JoystickGetHat(joystick_, 0);
        state_.buttons[static_cast<size_t>(Button::DPad_Up)]    = (hat & SDL_HAT_UP) != 0;
        state_.buttons[static_cast<size_t>(Button::DPad_Down)]  = (hat & SDL_HAT_DOWN) != 0;
        state_.buttons[static_cast<size_t>(Button::DPad_Left)]  = (hat & SDL_HAT_LEFT) != 0;
        state_.buttons[static_cast<size_t>(Button::DPad_Right)] = (hat & SDL_HAT_RIGHT) != 0;
    }

    return true;
}

bool SDLDevice::setLED(const Color& color) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (joystick_) {
        return SDL_JoystickSetLED(joystick_, color.r, color.g, color.b) == 0;
    }
#else
    (void)color;
#endif
    return false;
}

bool SDLDevice::setRumble(const Rumble& rumble) {
#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (joystick_) {
        // SDL rumble takes 0-65535; our Rumble is 0-255
        Uint16 low  = static_cast<Uint16>(rumble.left_motor)  * 257;
        Uint16 high = static_cast<Uint16>(rumble.right_motor) * 257;
        return SDL_JoystickRumble(joystick_, low, high, 250) == 0; // 250ms duration
    }
#else
    (void)rumble;
#endif
    return false;
}

// ── Public API (SDL available) ──────────────────────────────────────────────

bool initializeSDL() {
    if (g_sdl_initialized) return true;

    // Only init joystick + gamecontroller subsystems (not video/audio)
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "[gcpad] SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Enable background joystick events so we can poll without a window
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    g_sdl_initialized = true;
    return true;
}

void shutdownSDL() {
    if (g_sdl_initialized) {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
        g_sdl_initialized = false;
    }
}

std::vector<SDLDeviceInfo> enumerateSDLDevices() {
    std::vector<SDLDeviceInfo> result;
    if (!g_sdl_initialized) return result;

    SDL_PumpEvents();

    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; ++i) {
        SDLDeviceInfo info;
        info.sdl_joystick_index = i;
        info.is_game_controller = SDL_IsGameController(i) == SDL_TRUE;

        if (info.is_game_controller) {
            const char* n = SDL_GameControllerNameForIndex(i);
            info.name = n ? n : "Unknown GameController";
        } else {
            const char* n = SDL_JoystickNameForIndex(i);
            info.name = n ? n : "Unknown Joystick";
        }

        // VID/PID (SDL 2.0.6+)
#if SDL_VERSION_ATLEAST(2, 0, 6)
        info.vendor_id  = SDL_JoystickGetDeviceVendor(i);
        info.product_id = SDL_JoystickGetDeviceProduct(i);
#else
        info.vendor_id = 0;
        info.product_id = 0;
#endif

        result.push_back(std::move(info));
    }

    return result;
}

std::unique_ptr<GamepadDevice> createSDLDevice(int sdl_joystick_index, int slot) {
    return std::make_unique<SDLDevice>(sdl_joystick_index, slot);
}

#else // !GCPAD_HAS_SDL2

// ── Stubs when SDL2 is not available ────────────────────────────────────────

bool initializeSDL() { return false; }
void shutdownSDL() {}
std::vector<SDLDeviceInfo> enumerateSDLDevices() { return {}; }
std::unique_ptr<GamepadDevice> createSDLDevice(int, int) { return nullptr; }

#endif // GCPAD_HAS_SDL2

} // namespace internal
} // namespace gcpad
