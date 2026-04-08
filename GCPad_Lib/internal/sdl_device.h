#pragma once

#include "gcpad.h"
#include "GamepadManager.h"
#include <memory>
#include <vector>
#include <string>

namespace gcpad {
namespace internal {

/// Information about an SDL-discovered device.
struct SDLDeviceInfo {
    int sdl_joystick_index;    // SDL device index at enumeration time
    std::string name;
    uint16_t vendor_id;
    uint16_t product_id;
    bool is_game_controller;   // true if SDL has a GameController mapping
};

/// Create a GamepadDevice backed by SDL2 GameController / Joystick API.
GCPAD_API std::unique_ptr<GamepadDevice> createSDLDevice(
    int sdl_joystick_index, int slot);

/// Enumerate all SDL joysticks. Pumps SDL events internally.
GCPAD_API std::vector<SDLDeviceInfo> enumerateSDLDevices();

/// Initialize SDL joystick + gamecontroller subsystems (call once).
GCPAD_API bool initializeSDL();

/// Shutdown SDL subsystems.
GCPAD_API void shutdownSDL();

} // namespace internal
} // namespace gcpad
