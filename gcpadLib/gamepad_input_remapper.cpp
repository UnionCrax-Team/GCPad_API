#include "gamepad_input_remapper.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace gcpad {

GamepadInputRemapper::GamepadInputRemapper() {
    clearButtonMappings();
    clearAxisMappings();
}

void GamepadInputRemapper::mapButtonToKey(Button button, uint16_t virtual_key) {
    button_to_key[static_cast<size_t>(button)] = virtual_key;
}

void GamepadInputRemapper::mapButtonToMouseButton(Button button, MouseButton mouse_button) {
    button_to_mouse[static_cast<size_t>(button)] = mouse_button;
}

void GamepadInputRemapper::clearButtonMappings() {
    for (auto& b : button_to_key) b.reset();
    for (auto& b : button_to_mouse) b.reset();
}

void GamepadInputRemapper::mapAxisToMouse(Axis axis, float sensitivity, float deadzone, bool invert) {
    auto& mapping = axis_to_mouse[static_cast<size_t>(axis)];
    mapping.enabled = true;
    mapping.sensitivity = sensitivity;
    mapping.deadzone = deadzone;
    mapping.invert = invert;
}

void GamepadInputRemapper::clearAxisMappings() {
    for (auto& am : axis_to_mouse) {
        am.enabled = false;
        am.sensitivity = 20.0f;
        am.deadzone = 0.1f;
        am.invert = false;
    }
}

static int axisValueToMotion(float value, const AxisMouseMapping& mapping) {
    if (std::abs(value) <= mapping.deadzone) {
        return 0;
    }

    float normalized = (std::abs(value) - mapping.deadzone) / (1.0f - mapping.deadzone);
    if (mapping.invert) {
        normalized = -normalized;
    } else if (value < 0.0f) {
        normalized = -normalized;
    }

    return static_cast<int>(normalized * mapping.sensitivity);
}

std::vector<GamepadInputEvent> GamepadInputRemapper::remap(const GamepadState& current, const GamepadState& previous) const {
    std::vector<GamepadInputEvent> events;

    // Button events
    for (size_t i = 0; i < static_cast<size_t>(Button::COUNT); ++i) {
        bool curPressed = current.buttons[i];
        bool prevPressed = previous.buttons[i];

        if (curPressed != prevPressed) {
            if (button_to_key[i]) {
                GamepadInputEvent event{};
                event.type = GamepadInputEvent::Type::Keyboard;
                event.keyboard.virtual_key = *button_to_key[i];
                event.keyboard.pressed = curPressed;
                events.push_back(event);
            }
            if (button_to_mouse[i]) {
                GamepadInputEvent event{};
                event.type = GamepadInputEvent::Type::MouseButton;
                event.mouse_button.button = *button_to_mouse[i];
                event.mouse_button.pressed = curPressed;
                events.push_back(event);
            }
        }
    }

    // Axis events mapped to mouse motion
    int dx = 0;
    int dy = 0;

    if (axis_to_mouse[static_cast<size_t>(Axis::LeftX)].enabled) {
        dx += axisValueToMotion(current.axes[static_cast<size_t>(Axis::LeftX)], axis_to_mouse[static_cast<size_t>(Axis::LeftX)]);
    }
    if (axis_to_mouse[static_cast<size_t>(Axis::LeftY)].enabled) {
        dy += axisValueToMotion(current.axes[static_cast<size_t>(Axis::LeftY)], axis_to_mouse[static_cast<size_t>(Axis::LeftY)]);
    }
    if (axis_to_mouse[static_cast<size_t>(Axis::RightX)].enabled) {
        dx += axisValueToMotion(current.axes[static_cast<size_t>(Axis::RightX)], axis_to_mouse[static_cast<size_t>(Axis::RightX)]);
    }
    if (axis_to_mouse[static_cast<size_t>(Axis::RightY)].enabled) {
        dy += axisValueToMotion(current.axes[static_cast<size_t>(Axis::RightY)], axis_to_mouse[static_cast<size_t>(Axis::RightY)]);
    }

    if (dx != 0 || dy != 0) {
        GamepadInputEvent moveEvent{};
        moveEvent.type = GamepadInputEvent::Type::MouseMove;
        moveEvent.mouse_move.dx = dx;
        moveEvent.mouse_move.dy = dy;
        events.push_back(moveEvent);
    }

    return events;
}

bool GamepadInputRemapper::sendInput(const GamepadState& current, const GamepadState& previous) const {
    auto events = remap(current, previous);
    if (events.empty()) {
        return true;
    }

#ifdef _WIN32
    std::vector<INPUT> native;
    native.reserve(events.size());

    for (const auto& e : events) {
        switch (e.type) {
            case GamepadInputEvent::Type::Keyboard: {
                INPUT input = {};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = e.keyboard.virtual_key;
                input.ki.dwFlags = e.keyboard.pressed ? 0 : KEYEVENTF_KEYUP;
                native.push_back(input);
                break;
            }
            case GamepadInputEvent::Type::MouseButton: {
                INPUT input = {};
                input.type = INPUT_MOUSE;
                switch (e.mouse_button.button) {
                    case MouseButton::Left:
                        input.mi.dwFlags = e.mouse_button.pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                        break;
                    case MouseButton::Right:
                        input.mi.dwFlags = e.mouse_button.pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                        break;
                    case MouseButton::Middle:
                        input.mi.dwFlags = e.mouse_button.pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
                        break;
                }
                native.push_back(input);
                break;
            }
            case GamepadInputEvent::Type::MouseMove: {
                INPUT input = {};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_MOVE;
                input.mi.dx = e.mouse_move.dx;
                input.mi.dy = e.mouse_move.dy;
                native.push_back(input);
                break;
            }
            case GamepadInputEvent::Type::MouseWheel: {
                INPUT input = {};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                input.mi.mouseData = e.mouse_wheel.delta;
                native.push_back(input);
                break;
            }
        }
    }

    if (!native.empty()) {
        UINT sent = SendInput(static_cast<UINT>(native.size()), native.data(), sizeof(INPUT));
        return sent == native.size();
    }

    return true;
#else
    (void)current;
    (void)previous;
    (void)events;
    return false;
#endif
}

} // namespace gcpad
