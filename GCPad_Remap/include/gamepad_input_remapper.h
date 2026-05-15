#pragma once

#include <array>
#include <optional>
#include <vector>
#include <cstdint>
#include <string>
#include <map>

#include "gcpad.h"

#ifdef _WIN32
  #ifdef GCPAD_REMAP_EXPORTS
    #define GCPAD_REMAP_API __declspec(dllexport)
  #else
    #define GCPAD_REMAP_API __declspec(dllimport)
  #endif
#else
  // The remap module is compiled into libgcpad.so, which on Linux is built
  // with -fvisibility=hidden. Mark the public class with default visibility
  // explicitly so frontends can link against these symbols.
  #define GCPAD_REMAP_API __attribute__((visibility("default")))
#endif

namespace gcpad {

// ── Output event types ───────────────────────────────────────────────────────

enum class MouseButton {
    Left,
    Right,
    Middle,
};

struct MouseMove {
    int dx = 0;
    int dy = 0;
};

struct KeyboardEvent {
    uint16_t virtual_key = 0;
    uint16_t scan_code   = 0;  // hardware scan code for DirectInput/RawInput
    bool pressed = false;
};

struct MouseButtonEvent {
    MouseButton button = MouseButton::Left;
    bool pressed = false;
};

struct MouseWheelEvent {
    int delta = 0;
};

struct GamepadInputEvent {
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

// ── Mapping configuration structures ─────────────────────────────────────────

// Axis-to-mouse motion mapping (for analog sticks controlling cursor)
struct AxisMouseMapping {
    bool enabled = false;
    // Pixels per frame at full deflection (before sub-pixel accumulation).
    // ~25 at 60 fps ≈ 1500 px/s feels natural for desktop/game cursor work.
    float sensitivity = 25.0f;
    float deadzone = 0.12f;
    bool invert = false;
    // Response curve exponent applied after deadzone removal.
    // 1.0 = linear (twitchy), 1.5 = slightly curved (recommended),
    // 2.0+ = strong curve (precise but sluggish at low deflection).
    float curve = 1.5f;
};

// Axis-to-key mapping (e.g. trigger past threshold -> key press,
// or stick fully left -> press 'A')
struct AxisKeyMapping {
    bool enabled = false;
    uint16_t virtual_key = 0;
    // Threshold at which the axis "fires" the key.
    // For triggers (0..1): typically 0.5
    // For sticks (-1..1): positive threshold triggers on positive deflection,
    //                      negative threshold triggers on negative deflection.
    float threshold = 0.5f;
    // If true, the key fires when axis value < threshold (for negative stick directions)
    bool negative_direction = false;
};

// Axis-to-mouse-button mapping (e.g. trigger -> left click)
struct AxisMouseButtonMapping {
    bool enabled = false;
    MouseButton button = MouseButton::Left;
    float threshold = 0.5f;
};

// Axis-to-scroll-wheel mapping (e.g. right stick Y -> scroll)
struct AxisWheelMapping {
    bool enabled = false;
    int delta_per_tick = 120;  // WHEEL_DELTA
    float deadzone = 0.2f;
    bool invert = false;
    // Accumulator threshold: how much axis-time accumulates before a tick fires
    float tick_rate = 0.15f;
};

// Button-to-scroll-wheel mapping (e.g. L1 -> wheel up, R1 -> wheel down)
struct ButtonWheelMapping {
    bool enabled = false;
    int delta = 120;  // positive = up, negative = down
};

// ── Main remapper class ──────────────────────────────────────────────────────

struct GCPAD_REMAP_API GamepadInputRemapper : public Remapper {
    // Button -> keyboard key
    std::array<std::optional<uint16_t>, static_cast<size_t>(Button::COUNT)> button_to_key;
    // Button -> mouse button
    std::array<std::optional<MouseButton>, static_cast<size_t>(Button::COUNT)> button_to_mouse;
    // Button -> scroll wheel
    std::array<ButtonWheelMapping, static_cast<size_t>(Button::COUNT)> button_to_wheel;

    // Axis -> mouse motion (typically left/right stick)
    std::array<AxisMouseMapping, static_cast<size_t>(Axis::COUNT)> axis_to_mouse;
    // Axis -> keyboard key (threshold-based, up to 2 per axis for +/- directions)
    std::array<std::optional<AxisKeyMapping>, static_cast<size_t>(Axis::COUNT)> axis_to_key_positive;
    std::array<std::optional<AxisKeyMapping>, static_cast<size_t>(Axis::COUNT)> axis_to_key_negative;
    // Axis -> mouse button (e.g. trigger -> click)
    std::array<std::optional<AxisMouseButtonMapping>, static_cast<size_t>(Axis::COUNT)> axis_to_mouse_button;
    // Axis -> mouse wheel
    std::array<std::optional<AxisWheelMapping>, static_cast<size_t>(Axis::COUNT)> axis_to_wheel;

    GamepadInputRemapper();

    // ── Button mapping ───────────────────────────────────────────────────────
    void mapButtonToKey(Button button, uint16_t virtual_key);
    void mapButtonToMouseButton(Button button, MouseButton mouse_button);
    void mapButtonToWheel(Button button, int delta = 120);
    void clearButtonMapping(Button button);
    void clearAllButtonMappings();

    // ── Axis mapping ─────────────────────────────────────────────────────────
    void mapAxisToMouse(Axis axis, float sensitivity = 15.0f, float deadzone = 0.15f,
                        bool invert = false, float curve = 2.0f);
    void mapAxisToKey(Axis axis, uint16_t virtual_key, float threshold = 0.5f,
                      bool negative_direction = false);
    void mapAxisToMouseButton(Axis axis, MouseButton mouse_button, float threshold = 0.5f);
    void mapAxisToWheel(Axis axis, int delta = 120, float deadzone = 0.2f,
                        bool invert = false, float tick_rate = 0.15f);
    void clearAxisMapping(Axis axis);
    void clearAllAxisMappings();

    // ── Processing ───────────────────────────────────────────────────────────

    // Generate events by comparing current vs previous state. Does NOT send them.
    std::vector<GamepadInputEvent> remap(const GamepadState& current,
                                         const GamepadState& previous) const;

    // Generate events AND inject them into the OS via SendInput.
    // Returns true if all events were sent successfully.
    bool sendInput(const GamepadState& current, const GamepadState& previous);

    // Reset accumulated state (call when switching profiles or deactivating)
    void resetState();

    // ── Persistence helpers ──────────────────────────────────────────────────

    // Human-readable name for virtual key codes (for UI display)
    static std::string virtualKeyName(uint16_t vk);

private:
    // Sub-pixel mouse motion accumulators: fractional pixel remainder kept
    // across frames so gentle stick deflections don't vanish on truncation.
    mutable float mouse_accum_x_ = 0.0f;
    mutable float mouse_accum_y_ = 0.0f;
    // Scroll wheel accumulators (mutable because remap() needs to update them
    // even in a const-like usage pattern; sendInput is non-const anyway)
    mutable std::array<float, static_cast<size_t>(Axis::COUNT)> wheel_accum_;
    // Track which axis-triggered keys/buttons are currently "pressed" to
    // generate proper press/release transitions
    mutable std::array<bool, static_cast<size_t>(Axis::COUNT)> axis_key_pos_active_;
    mutable std::array<bool, static_cast<size_t>(Axis::COUNT)> axis_key_neg_active_;
    mutable std::array<bool, static_cast<size_t>(Axis::COUNT)> axis_mouse_btn_active_;
};

} // namespace gcpad
