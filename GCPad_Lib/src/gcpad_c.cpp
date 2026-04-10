/**
 * gcpad_c.cpp — Plain C ABI implementation for the GCPad library.
 *
 * Wraps the C++ GamepadManager in an opaque handle and exposes a stable
 * set of C-linkage functions declared in gcpad_c.h.
 */

#include "gcpad_c.h"
#include "GamepadManager.h"
#include "gamepad_input_remapper.h"

#include <memory>
#include <cstring>
#include <cstdio>

// ── Internal wrapper ──────────────────────────────────────────────────────────

struct GCPadManagerWrapper {
    std::unique_ptr<gcpad::GamepadManager> mgr;

    GCPadHotplugCallback connected_cb   = nullptr;
    void*                connected_ud   = nullptr;
    GCPadHotplugCallback disconnected_cb = nullptr;
    void*                disconnected_ud = nullptr;
};

struct GCPadRemapperWrapper {
    std::shared_ptr<gcpad::GamepadInputRemapper> remapper;
    gcpad::GamepadState previous_state;
};

static inline GCPadManagerWrapper* to_wrapper(GCPadManagerHandle h) {
    return static_cast<GCPadManagerWrapper*>(h);
}

static inline GCPadRemapperWrapper* to_remapper_wrapper(GCPadRemapperHandle h) {
    return static_cast<GCPadRemapperWrapper*>(h);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

extern "C" {

GCPadManagerHandle gcpad_create_manager() {
    auto* w = new (std::nothrow) GCPadManagerWrapper();
    if (!w) return nullptr;
    w->mgr = gcpad::createGamepadManager();
    if (!w->mgr) { delete w; return nullptr; }
    return static_cast<GCPadManagerHandle>(w);
}

void gcpad_destroy_manager(GCPadManagerHandle mgr) {
    if (!mgr) return;
    delete to_wrapper(mgr);
}

int gcpad_initialize(GCPadManagerHandle mgr) {
    if (!mgr) return 0;
    auto* w = to_wrapper(mgr);

    // Wire the C++ callbacks through our C-ABI callbacks.
    // The lambdas read from w->connected_cb at call time, so they work even
    // if the C-ABI callbacks are registered after this call.
    w->mgr->setGamepadConnectedCallback([w](int slot) {
        if (w->connected_cb) w->connected_cb(slot, w->connected_ud);
    });
    w->mgr->setGamepadDisconnectedCallback([w](int slot) {
        if (w->disconnected_cb) w->disconnected_cb(slot, w->disconnected_ud);
    });

    return w->mgr->initialize() ? 1 : 0;
}

void gcpad_shutdown(GCPadManagerHandle mgr) {
    if (!mgr) return;
    to_wrapper(mgr)->mgr->shutdown();
}

// ── Per-frame update ──────────────────────────────────────────────────────────

void gcpad_update_all(GCPadManagerHandle mgr) {
    if (!mgr) return;
    to_wrapper(mgr)->mgr->updateAll();
}

// ── Device queries ────────────────────────────────────────────────────────────

int gcpad_get_max_slots(GCPadManagerHandle mgr) {
    if (!mgr) return 0;
    return to_wrapper(mgr)->mgr->getMaxGamepads();
}

int gcpad_get_state(GCPadManagerHandle mgr, int slot, GCPadStateC* out) {
    if (!mgr || !out) return 0;
    std::memset(out, 0, sizeof(GCPadStateC));

    auto* w = to_wrapper(mgr);
    const gcpad::GamepadDevice* dev = w->mgr->getGamepad(slot);
    if (!dev || !dev->isConnected()) return 0;

    const gcpad::GamepadState& st = dev->getState();

    static_assert(GCPAD_BUTTON_COUNT == static_cast<int>(gcpad::Button::COUNT),
                  "GCPAD_BUTTON_COUNT mismatch with gcpad::Button::COUNT");
    static_assert(GCPAD_AXIS_COUNT == static_cast<int>(gcpad::Axis::COUNT),
                  "GCPAD_AXIS_COUNT mismatch with gcpad::Axis::COUNT");

    for (int i = 0; i < GCPAD_BUTTON_COUNT; ++i)
        out->buttons[i] = st.buttons[static_cast<size_t>(i)] ? 1u : 0u;

    for (int i = 0; i < GCPAD_AXIS_COUNT; ++i)
        out->axes[i] = st.axes[static_cast<size_t>(i)];

    out->gyro_x  = st.gyro.x;  out->gyro_y  = st.gyro.y;  out->gyro_z  = st.gyro.z;
    out->accel_x = st.accel.x; out->accel_y = st.accel.y; out->accel_z = st.accel.z;
    out->battery_level = st.battery_level;
    out->is_charging   = st.is_charging ? 1u : 0u;
    out->is_connected  = 1u;

    // Touchpad contacts
    for (int i = 0; i < 2; ++i) {
        out->touchpad_active[i] = st.touchpad[static_cast<size_t>(i)].active ? 1u : 0u;
        out->touchpad_x[i]      = st.touchpad[static_cast<size_t>(i)].x;
        out->touchpad_y[i]      = st.touchpad[static_cast<size_t>(i)].y;
    }

    return 1;
}

int gcpad_get_name(GCPadManagerHandle mgr, int slot, char* buf, int buf_len) {
    if (!mgr || !buf || buf_len <= 0) return 0;
    buf[0] = '\0';
    const gcpad::GamepadDevice* dev = to_wrapper(mgr)->mgr->getGamepad(slot);
    if (!dev) return 0;
    std::string name = dev->getName();
    std::strncpy(buf, name.c_str(), static_cast<size_t>(buf_len - 1));
    buf[buf_len - 1] = '\0';
    return 1;
}

// ── Output ────────────────────────────────────────────────────────────────────

int gcpad_set_rumble(GCPadManagerHandle mgr, int slot,
                     uint8_t left_motor, uint8_t right_motor) {
    if (!mgr) return 0;
    gcpad::GamepadDevice* dev = to_wrapper(mgr)->mgr->getGamepad(slot);
    if (!dev || !dev->isConnected()) return 0;
    return dev->setRumble({ left_motor, right_motor }) ? 1 : 0;
}

int gcpad_set_led(GCPadManagerHandle mgr, int slot,
                  uint8_t r, uint8_t g, uint8_t b) {
    if (!mgr) return 0;
    gcpad::GamepadDevice* dev = to_wrapper(mgr)->mgr->getGamepad(slot);
    if (!dev || !dev->isConnected()) return 0;
    return dev->setLED({ r, g, b }) ? 1 : 0;
}

// ── DualSense-specific ───────────────────────────────────────────────────────

int gcpad_set_trigger_effect(GCPadManagerHandle mgr, int slot,
                              int right_trigger, uint8_t mode,
                              uint8_t start, uint8_t end,
                              uint8_t force, uint8_t param1, uint8_t param2) {
    if (!mgr) return 0;
    gcpad::GamepadDevice* dev = to_wrapper(mgr)->mgr->getGamepad(slot);
    if (!dev || !dev->isConnected()) return 0;
    gcpad::TriggerEffect effect;
    effect.mode   = static_cast<gcpad::TriggerEffect::Mode>(mode);
    effect.start  = start;
    effect.end    = end;
    effect.force  = force;
    effect.param1 = param1;
    effect.param2 = param2;
    return dev->setTriggerEffect(right_trigger != 0, effect) ? 1 : 0;
}

int gcpad_set_player_leds(GCPadManagerHandle mgr, int slot, uint8_t led_mask) {
    if (!mgr) return 0;
    gcpad::GamepadDevice* dev = to_wrapper(mgr)->mgr->getGamepad(slot);
    if (!dev || !dev->isConnected()) return 0;
    return dev->setPlayerLEDs(led_mask) ? 1 : 0;
}

// ── Hotplug callbacks ─────────────────────────────────────────────────────────

void gcpad_set_connected_callback(GCPadManagerHandle mgr,
                                   GCPadHotplugCallback cb,
                                   void* userdata) {
    if (!mgr) return;
    auto* w = to_wrapper(mgr);
    w->connected_cb = cb;
    w->connected_ud = userdata;
}

void gcpad_set_disconnected_callback(GCPadManagerHandle mgr,
                                      GCPadHotplugCallback cb,
                                      void* userdata) {
    if (!mgr) return;
    auto* w = to_wrapper(mgr);
    w->disconnected_cb = cb;
    w->disconnected_ud = userdata;
}

// ── Input Remapping ───────────────────────────────────────────────────────────

GCPadRemapperHandle gcpad_remapper_create() {
    auto* w = new (std::nothrow) GCPadRemapperWrapper();
    if (!w) return nullptr;
    w->remapper = std::make_shared<gcpad::GamepadInputRemapper>();
    if (!w->remapper) { delete w; return nullptr; }
    return static_cast<GCPadRemapperHandle>(w);
}

void gcpad_remapper_destroy(GCPadRemapperHandle remapper) {
    if (!remapper) return;
    delete to_remapper_wrapper(remapper);
}

void gcpad_remapper_map_button_to_key(GCPadRemapperHandle remapper, int button, uint16_t virtual_key) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->mapButtonToKey(static_cast<gcpad::Button>(button), virtual_key);
}

void gcpad_remapper_map_button_to_mouse(GCPadRemapperHandle remapper, int button, int mouse_button) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->mapButtonToMouseButton(
        static_cast<gcpad::Button>(button),
        static_cast<gcpad::MouseButton>(mouse_button)
    );
}

void gcpad_remapper_map_button_to_wheel(GCPadRemapperHandle remapper, int button, int delta) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->mapButtonToWheel(static_cast<gcpad::Button>(button), delta);
}

void gcpad_remapper_map_axis_to_mouse(GCPadRemapperHandle remapper, int axis, float sensitivity, float deadzone, int invert, float curve) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->mapAxisToMouse(
        static_cast<gcpad::Axis>(axis),
        sensitivity, deadzone, invert != 0, curve
    );
}

void gcpad_remapper_map_axis_to_key(GCPadRemapperHandle remapper, int axis, uint16_t virtual_key, float threshold, int negative_direction) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->mapAxisToKey(
        static_cast<gcpad::Axis>(axis),
        virtual_key, threshold, negative_direction != 0
    );
}

void gcpad_remapper_map_axis_to_mouse_button(GCPadRemapperHandle remapper, int axis, int mouse_button, float threshold) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->mapAxisToMouseButton(
        static_cast<gcpad::Axis>(axis),
        static_cast<gcpad::MouseButton>(mouse_button),
        threshold
    );
}

void gcpad_remapper_map_axis_to_wheel(GCPadRemapperHandle remapper, int axis, int delta, float deadzone, int invert, float tick_rate) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->mapAxisToWheel(
        static_cast<gcpad::Axis>(axis),
        delta, deadzone, invert != 0, tick_rate
    );
}

void gcpad_remapper_clear_all(GCPadRemapperHandle remapper) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->clearAllButtonMappings();
    w->remapper->clearAllAxisMappings();
}

int gcpad_remapper_send_input(GCPadRemapperHandle remapper, GCPadStateC* current_state, GCPadStateC* previous_state) {
    if (!remapper || !current_state) return 0;
    auto* w = to_remapper_wrapper(remapper);

    gcpad::GamepadState current;
    for (int i = 0; i < GCPAD_BUTTON_COUNT; ++i) {
        current.buttons[i] = current_state->buttons[i] != 0;
    }
    for (int i = 0; i < GCPAD_AXIS_COUNT; ++i) {
        current.axes[i] = current_state->axes[i];
    }

    gcpad::GamepadState previous;
    if (previous_state) {
        for (int i = 0; i < GCPAD_BUTTON_COUNT; ++i) {
            previous.buttons[i] = previous_state->buttons[i] != 0;
        }
        for (int i = 0; i < GCPAD_AXIS_COUNT; ++i) {
            previous.axes[i] = previous_state->axes[i];
        }
    }

    return w->remapper->sendInput(current, previous) ? 1 : 0;
}

void gcpad_remapper_reset_state(GCPadRemapperHandle remapper) {
    if (!remapper) return;
    auto* w = to_remapper_wrapper(remapper);
    w->remapper->resetState();
}

void gcpad_set_remapper(GCPadManagerHandle mgr, int slot, GCPadRemapperHandle remapper) {
    if (!mgr) return;
    auto* w = to_wrapper(mgr);
    if (!remapper) {
        w->mgr->setRemapper(nullptr);
        return;
    }
    auto* rw = to_remapper_wrapper(remapper);
    w->mgr->setRemapper(rw->remapper);
}

} // extern "C"
