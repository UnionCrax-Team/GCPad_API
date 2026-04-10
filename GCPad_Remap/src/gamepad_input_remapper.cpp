#include "gamepad_input_remapper.h"

#include <cmath>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>
#endif

namespace gcpad {

// ── Helpers ──────────────────────────────────────────────────────────────────

// Convert a virtual key code to the corresponding hardware scan code.
// Many games read scan codes via DirectInput or RawInput instead of virtual
// key codes, so we must provide both for reliable injection.
static uint16_t vkToScanCode(uint16_t vk) {
#ifdef _WIN32
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    return static_cast<uint16_t>(sc);
#else
    (void)vk;
    return 0;
#endif
}

// Apply deadzone removal and a power response curve to a raw axis value.
// Returns a signed value in [-1, 1] with the deadzone range collapsed to 0.
static float applyDeadzoneAndCurve(float value, float deadzone, float curve) {
    float absVal = std::abs(value);
    if (absVal <= deadzone) {
        return 0.0f;
    }
    // Normalize from [deadzone, 1] to [0, 1]
    float normalized = (absVal - deadzone) / (1.0f - deadzone);
    normalized = std::fmin(normalized, 1.0f);
    // Apply power curve for acceleration
    float curved = std::pow(normalized, curve);
    // Restore original sign
    return (value < 0.0f) ? -curved : curved;
}

// Convert a processed axis value to pixel motion for mouse movement.
static int axisToMouseMotion(float value, const AxisMouseMapping& mapping) {
    float processed = applyDeadzoneAndCurve(value, mapping.deadzone, mapping.curve);
    if (processed == 0.0f) {
        return 0;
    }
    if (mapping.invert) {
        processed = -processed;
    }
    return static_cast<int>(processed * mapping.sensitivity);
}

// ── Construction / reset ─────────────────────────────────────────────────────

GamepadInputRemapper::GamepadInputRemapper() {
    clearAllButtonMappings();
    clearAllAxisMappings();
    resetState();
}

void GamepadInputRemapper::resetState() {
    wheel_accum_.fill(0.0f);
    axis_key_pos_active_.fill(false);
    axis_key_neg_active_.fill(false);
    axis_mouse_btn_active_.fill(false);
}

// ── Button mapping ───────────────────────────────────────────────────────────

void GamepadInputRemapper::mapButtonToKey(Button button, uint16_t virtual_key) {
    button_to_key[static_cast<size_t>(button)] = virtual_key;
}

void GamepadInputRemapper::mapButtonToMouseButton(Button button, MouseButton mouse_button) {
    button_to_mouse[static_cast<size_t>(button)] = mouse_button;
}

void GamepadInputRemapper::mapButtonToWheel(Button button, int delta) {
    auto& m = button_to_wheel[static_cast<size_t>(button)];
    m.enabled = true;
    m.delta = delta;
}

void GamepadInputRemapper::clearButtonMapping(Button button) {
    size_t idx = static_cast<size_t>(button);
    button_to_key[idx].reset();
    button_to_mouse[idx].reset();
    button_to_wheel[idx].enabled = false;
}

void GamepadInputRemapper::clearAllButtonMappings() {
    for (auto& b : button_to_key)  b.reset();
    for (auto& b : button_to_mouse) b.reset();
    for (auto& b : button_to_wheel) b.enabled = false;
}

// ── Axis mapping ─────────────────────────────────────────────────────────────

void GamepadInputRemapper::mapAxisToMouse(Axis axis, float sensitivity, float deadzone,
                                           bool invert, float curve) {
    auto& m = axis_to_mouse[static_cast<size_t>(axis)];
    m.enabled     = true;
    m.sensitivity = sensitivity;
    m.deadzone    = deadzone;
    m.invert      = invert;
    m.curve       = curve;
}

void GamepadInputRemapper::mapAxisToKey(Axis axis, uint16_t virtual_key,
                                         float threshold, bool negative_direction) {
    size_t idx = static_cast<size_t>(axis);
    AxisKeyMapping akm;
    akm.enabled             = true;
    akm.virtual_key         = virtual_key;
    akm.threshold           = std::abs(threshold);
    akm.negative_direction  = negative_direction;

    if (negative_direction) {
        axis_to_key_negative[idx] = akm;
    } else {
        axis_to_key_positive[idx] = akm;
    }
}

void GamepadInputRemapper::mapAxisToMouseButton(Axis axis, MouseButton mouse_button,
                                                 float threshold) {
    size_t idx = static_cast<size_t>(axis);
    AxisMouseButtonMapping abm;
    abm.enabled   = true;
    abm.button    = mouse_button;
    abm.threshold = threshold;
    axis_to_mouse_button[idx] = abm;
}

void GamepadInputRemapper::mapAxisToWheel(Axis axis, int delta, float deadzone,
                                           bool invert, float tick_rate) {
    size_t idx = static_cast<size_t>(axis);
    AxisWheelMapping awm;
    awm.enabled        = true;
    awm.delta_per_tick = delta;
    awm.deadzone       = deadzone;
    awm.invert         = invert;
    awm.tick_rate      = tick_rate;
    axis_to_wheel[idx] = awm;
}

void GamepadInputRemapper::clearAxisMapping(Axis axis) {
    size_t idx = static_cast<size_t>(axis);
    axis_to_mouse[idx]        = AxisMouseMapping{};
    axis_to_key_positive[idx].reset();
    axis_to_key_negative[idx].reset();
    axis_to_mouse_button[idx].reset();
    axis_to_wheel[idx].reset();
}

void GamepadInputRemapper::clearAllAxisMappings() {
    for (auto& am : axis_to_mouse) am = AxisMouseMapping{};
    for (auto& ak : axis_to_key_positive)  ak.reset();
    for (auto& ak : axis_to_key_negative)  ak.reset();
    for (auto& ab : axis_to_mouse_button)  ab.reset();
    for (auto& aw : axis_to_wheel)         aw.reset();
}

// ── Event generation ─────────────────────────────────────────────────────────

std::vector<GamepadInputEvent> GamepadInputRemapper::remap(
        const GamepadState& current, const GamepadState& previous) const {
    std::vector<GamepadInputEvent> events;

    // ── Button -> keyboard / mouse button / mouse wheel events ───────────────
    for (size_t i = 0; i < static_cast<size_t>(Button::COUNT); ++i) {
        bool curPressed  = current.buttons[i];
        bool prevPressed = previous.buttons[i];

        if (curPressed != prevPressed) {
            // Button -> keyboard key
            if (button_to_key[i]) {
                GamepadInputEvent ev{};
                ev.type = GamepadInputEvent::Type::Keyboard;
                ev.keyboard.virtual_key = *button_to_key[i];
                ev.keyboard.scan_code   = vkToScanCode(*button_to_key[i]);
                ev.keyboard.pressed     = curPressed;
                events.push_back(ev);
            }
            // Button -> mouse button
            if (button_to_mouse[i]) {
                GamepadInputEvent ev{};
                ev.type = GamepadInputEvent::Type::MouseButton;
                ev.mouse_button.button  = *button_to_mouse[i];
                ev.mouse_button.pressed = curPressed;
                events.push_back(ev);
            }
            // Button -> mouse wheel (only on press, not release)
            if (button_to_wheel[i].enabled && curPressed) {
                GamepadInputEvent ev{};
                ev.type = GamepadInputEvent::Type::MouseWheel;
                ev.mouse_wheel.delta = button_to_wheel[i].delta;
                events.push_back(ev);
            }
        }
    }

    // ── Axis -> mouse motion ─────────────────────────────────────────────────
    int dx = 0, dy = 0;

    auto accumulateAxis = [&](Axis axis, int& target_x, int& target_y, bool isX) {
        size_t idx = static_cast<size_t>(axis);
        if (!axis_to_mouse[idx].enabled) return;
        int motion = axisToMouseMotion(current.axes[idx], axis_to_mouse[idx]);
        if (isX) target_x += motion;
        else     target_y += motion;
    };

    accumulateAxis(Axis::LeftX,  dx, dy, true);
    accumulateAxis(Axis::LeftY,  dx, dy, false);
    accumulateAxis(Axis::RightX, dx, dy, true);
    accumulateAxis(Axis::RightY, dx, dy, false);

    // Triggers can also map to mouse motion (unusual but supported)
    accumulateAxis(Axis::LeftTrigger,  dx, dy, true);
    accumulateAxis(Axis::RightTrigger, dx, dy, true);

    if (dx != 0 || dy != 0) {
        GamepadInputEvent ev{};
        ev.type = GamepadInputEvent::Type::MouseMove;
        ev.mouse_move.dx = dx;
        ev.mouse_move.dy = dy;
        events.push_back(ev);
    }

    // ── Axis -> keyboard key (threshold-based) ──────────────────────────────
    for (size_t i = 0; i < static_cast<size_t>(Axis::COUNT); ++i) {
        float value = current.axes[i];

        // Positive direction
        if (axis_to_key_positive[i]) {
            const auto& akm = *axis_to_key_positive[i];
            if (akm.enabled) {
                bool shouldBeActive = (value >= akm.threshold);
                if (shouldBeActive && !axis_key_pos_active_[i]) {
                    GamepadInputEvent ev{};
                    ev.type = GamepadInputEvent::Type::Keyboard;
                    ev.keyboard.virtual_key = akm.virtual_key;
                    ev.keyboard.scan_code   = vkToScanCode(akm.virtual_key);
                    ev.keyboard.pressed     = true;
                    events.push_back(ev);
                    axis_key_pos_active_[i] = true;
                } else if (!shouldBeActive && axis_key_pos_active_[i]) {
                    GamepadInputEvent ev{};
                    ev.type = GamepadInputEvent::Type::Keyboard;
                    ev.keyboard.virtual_key = akm.virtual_key;
                    ev.keyboard.scan_code   = vkToScanCode(akm.virtual_key);
                    ev.keyboard.pressed     = false;
                    events.push_back(ev);
                    axis_key_pos_active_[i] = false;
                }
            }
        }

        // Negative direction
        if (axis_to_key_negative[i]) {
            const auto& akm = *axis_to_key_negative[i];
            if (akm.enabled) {
                bool shouldBeActive = (value <= -akm.threshold);
                if (shouldBeActive && !axis_key_neg_active_[i]) {
                    GamepadInputEvent ev{};
                    ev.type = GamepadInputEvent::Type::Keyboard;
                    ev.keyboard.virtual_key = akm.virtual_key;
                    ev.keyboard.scan_code   = vkToScanCode(akm.virtual_key);
                    ev.keyboard.pressed     = true;
                    events.push_back(ev);
                    axis_key_neg_active_[i] = true;
                } else if (!shouldBeActive && axis_key_neg_active_[i]) {
                    GamepadInputEvent ev{};
                    ev.type = GamepadInputEvent::Type::Keyboard;
                    ev.keyboard.virtual_key = akm.virtual_key;
                    ev.keyboard.scan_code   = vkToScanCode(akm.virtual_key);
                    ev.keyboard.pressed     = false;
                    events.push_back(ev);
                    axis_key_neg_active_[i] = false;
                }
            }
        }
    }

    // ── Axis -> mouse button (threshold-based) ──────────────────────────────
    for (size_t i = 0; i < static_cast<size_t>(Axis::COUNT); ++i) {
        if (!axis_to_mouse_button[i]) continue;
        const auto& abm = *axis_to_mouse_button[i];
        if (!abm.enabled) continue;

        float value = current.axes[i];
        bool shouldBeActive = (value >= abm.threshold);

        if (shouldBeActive && !axis_mouse_btn_active_[i]) {
            GamepadInputEvent ev{};
            ev.type = GamepadInputEvent::Type::MouseButton;
            ev.mouse_button.button  = abm.button;
            ev.mouse_button.pressed = true;
            events.push_back(ev);
            axis_mouse_btn_active_[i] = true;
        } else if (!shouldBeActive && axis_mouse_btn_active_[i]) {
            GamepadInputEvent ev{};
            ev.type = GamepadInputEvent::Type::MouseButton;
            ev.mouse_button.button  = abm.button;
            ev.mouse_button.pressed = false;
            events.push_back(ev);
            axis_mouse_btn_active_[i] = false;
        }
    }

    // ── Axis -> mouse wheel (accumulator-based) ─────────────────────────────
    for (size_t i = 0; i < static_cast<size_t>(Axis::COUNT); ++i) {
        if (!axis_to_wheel[i]) continue;
        const auto& awm = *axis_to_wheel[i];
        if (!awm.enabled) continue;

        float value = current.axes[i];
        if (std::abs(value) <= awm.deadzone) {
            wheel_accum_[i] = 0.0f;
            continue;
        }

        float contribution = value * awm.tick_rate;
        if (awm.invert) contribution = -contribution;
        wheel_accum_[i] += contribution;

        if (std::abs(wheel_accum_[i]) >= 1.0f) {
            int ticks = static_cast<int>(wheel_accum_[i]);
            wheel_accum_[i] -= static_cast<float>(ticks);

            GamepadInputEvent ev{};
            ev.type = GamepadInputEvent::Type::MouseWheel;
            ev.mouse_wheel.delta = ticks * awm.delta_per_tick;
            events.push_back(ev);
        }
    }

    return events;
}

// ── OS input injection ───────────────────────────────────────────────────────

bool GamepadInputRemapper::sendInput(const GamepadState& current,
                                      const GamepadState& previous) {
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
                input.ki.wVk   = e.keyboard.virtual_key;
                input.ki.wScan = e.keyboard.scan_code;
                // Use both virtual key and scan code for maximum compatibility.
                // Games using DirectInput/RawInput read the scan code;
                // games using GetAsyncKeyState read the virtual key.
                input.ki.dwFlags = KEYEVENTF_SCANCODE;
                if (!e.keyboard.pressed) {
                    input.ki.dwFlags |= KEYEVENTF_KEYUP;
                }
                // Extended keys (arrows, numpad enter, etc.) need the extended flag
                if (e.keyboard.scan_code > 0xFF ||
                    e.keyboard.virtual_key == VK_RIGHT ||
                    e.keyboard.virtual_key == VK_LEFT ||
                    e.keyboard.virtual_key == VK_UP ||
                    e.keyboard.virtual_key == VK_DOWN ||
                    e.keyboard.virtual_key == VK_INSERT ||
                    e.keyboard.virtual_key == VK_DELETE ||
                    e.keyboard.virtual_key == VK_HOME ||
                    e.keyboard.virtual_key == VK_END ||
                    e.keyboard.virtual_key == VK_PRIOR ||
                    e.keyboard.virtual_key == VK_NEXT ||
                    e.keyboard.virtual_key == VK_NUMLOCK) {
                    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
                }
                native.push_back(input);
                break;
            }
            case GamepadInputEvent::Type::MouseButton: {
                INPUT input = {};
                input.type = INPUT_MOUSE;
                switch (e.mouse_button.button) {
                    case MouseButton::Left:
                        input.mi.dwFlags = e.mouse_button.pressed
                            ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                        break;
                    case MouseButton::Right:
                        input.mi.dwFlags = e.mouse_button.pressed
                            ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                        break;
                    case MouseButton::Middle:
                        input.mi.dwFlags = e.mouse_button.pressed
                            ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
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
                input.mi.mouseData = static_cast<DWORD>(e.mouse_wheel.delta);
                native.push_back(input);
                break;
            }
        }
    }

    if (!native.empty()) {
        UINT sent = ::SendInput(static_cast<UINT>(native.size()),
                                native.data(), sizeof(INPUT));
        return sent == static_cast<UINT>(native.size());
    }

    return true;
#elif defined(__linux__)
    static Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return false;
    }

    Window root = DefaultRootWindow(display);
    Window focus;
    int revert_to;
    XGetInputFocus(display, &focus, &revert_to);

    for (const auto& e : events) {
        switch (e.type) {
            case GamepadInputEvent::Type::Keyboard: {
                KeyCode keycode = static_cast<KeyCode>(e.keyboard.virtual_key);
                if (e.keyboard.pressed) {
                    XTestFakeKeyEvent(display, keycode, True, CurrentTime);
                    XTestFakeKeyEvent(display, keycode, False, CurrentTime);
                }
                break;
            }
            case GamepadInputEvent::Type::MouseButton: {
                unsigned int button;
                switch (e.mouse_button.button) {
                    case MouseButton::Left: button = Button1; break;
                    case MouseButton::Right: button = Button2; break;
                    case MouseButton::Middle: button = Button3; break;
                    default: continue;
                }
                XTestFakeButtonEvent(display, button,
                    e.mouse_button.pressed ? True : False, CurrentTime);
                break;
            }
            case GamepadInputEvent::Type::MouseMove: {
                XTestFakeRelativeMotionEvent(display,
                    static_cast<double>(e.mouse_move.dx),
                    static_cast<double>(e.mouse_move.dy),
                    CurrentTime);
                break;
            }
            case GamepadInputEvent::Type::MouseWheel: {
                int direction = (e.mouse_wheel.delta > 0) ? 4 : 5;
                int count = std::abs(e.mouse_wheel.delta) / 120;
                for (int i = 0; i < count; ++i) {
                    XTestFakeButtonEvent(display, direction, True, CurrentTime);
                    XTestFakeButtonEvent(display, direction, False, CurrentTime);
                }
                break;
            }
        }
    }

    XFlush(display);
    return true;
#else
    (void)events;
    return false;
#endif
}

// ── Utility ──────────────────────────────────────────────────────────────────

std::string GamepadInputRemapper::virtualKeyName(uint16_t vk) {
#ifdef _WIN32
    // Use GetKeyNameTextA via scan code for readable names
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (sc == 0) {
        // Fallback for unmapped keys
        char buf[32];
        snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
        return buf;
    }
    // GetKeyNameTextA expects scan code in bits 16-23, extended flag in bit 24
    LONG lparam = static_cast<LONG>(sc) << 16;
    char name[64] = {};
    int len = GetKeyNameTextA(lparam, name, sizeof(name));
    if (len > 0) {
        return std::string(name, static_cast<size_t>(len));
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
    return buf;
#elif defined(__linux__)
    static Display* display = XOpenDisplay(nullptr);
    if (!display) {
        char buf[32];
        snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
        return buf;
    }
    KeyCode keycode = static_cast<KeyCode>(vk);
    KeySym keysym = XKeycodeToKeysym(display, keycode, 0);
    const char* name = XKeysymToString(keysym);
    if (name) {
        return std::string(name);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
    return buf;
#else
    char buf[32];
    snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
    return buf;
#endif
}

} // namespace gcpad
