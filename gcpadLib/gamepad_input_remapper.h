#pragma once

#include <array>
#include <optional>
#include <vector>
#include <cstdint>

#include "gcpad.h"

namespace gcpad {

enum class GCPAD_API MouseButton {
    Left,
    Right,
    Middle,
};

struct GCPAD_API MouseMove {
    int dx = 0;
    int dy = 0;
};

struct GCPAD_API KeyboardEvent {
    uint16_t virtual_key = 0;
    bool pressed = false;
};

struct GCPAD_API MouseButtonEvent {
    MouseButton button = MouseButton::Left;
    bool pressed = false;
};

struct GCPAD_API MouseWheelEvent {
    int delta = 0;
};

struct GCPAD_API GamepadInputEvent {
    enum class Type {
        Keyboard,
        MouseButton,
        MouseMove,
        MouseWheel,
    } type;

    KeyboardEvent keyboard;
    MouseButtonEvent mouse_button;
    MouseMove mouse_move;
    MouseWheelEvent mouse_wheel;
};

struct GCPAD_API AxisMouseMapping {
    bool enabled = false;
    float sensitivity = 20.0f;
    float deadzone = 0.1f;
    bool invert = false;
};

struct GCPAD_API GamepadInputRemapper {
    std::array<std::optional<uint16_t>, static_cast<size_t>(Button::COUNT)> button_to_key;
    std::array<std::optional<MouseButton>, static_cast<size_t>(Button::COUNT)> button_to_mouse;
    std::array<AxisMouseMapping, static_cast<size_t>(Axis::COUNT)> axis_to_mouse;

    GamepadInputRemapper();

    void mapButtonToKey(Button button, uint16_t virtual_key);
    void mapButtonToMouseButton(Button button, MouseButton mouse_button);
    void clearButtonMappings();

    void mapAxisToMouse(Axis axis, float sensitivity, float deadzone = 0.1f, bool invert = false);
    void clearAxisMappings();

    std::vector<GamepadInputEvent> remap(const GamepadState& current, const GamepadState& previous) const;

    bool sendInput(const GamepadState& current, const GamepadState& previous) const;
};

} // namespace gcpad
