/**
 * gcpad_c.cpp — Plain C ABI implementation for the GCPad library.
 *
 * Wraps the C++ GamepadManager in an opaque handle and exposes a stable
 * set of C-linkage functions declared in gcpad_c.h.
 */

#include "gcpad_c.h"
#include "GamepadManager.h"

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

static inline GCPadManagerWrapper* to_wrapper(GCPadManagerHandle h) {
    return static_cast<GCPadManagerWrapper*>(h);
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

} // extern "C"
