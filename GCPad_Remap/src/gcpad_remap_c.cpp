/**
 * gcpad_remap_c.cpp — C ABI for the input remapper.
 *
 * Provides the remapper functions that were previously in gcpad_c.cpp,
 * allowing gcpad.dll to be a standalone shared library.
 */

#include "gamepad_input_remapper.h"
#include "gcpad_c.h"

#include <memory>
#include <cstring>
#include <cstdio>

struct GCPadRemapperWrapper {
    std::shared_ptr<gcpad::GamepadInputRemapper> remapper;
    gcpad::GamepadState previous_state;
};

static inline GCPadRemapperWrapper* to_remapper_wrapper(GCPadRemapperHandle h) {
    return static_cast<GCPadRemapperWrapper*>(h);
}

extern "C" {

GCPadRemapperHandle gcpad_remapper_create() {
    auto* w = new (std::nothrow) GCPadRemapperWrapper();
    if (!w) return nullptr;
    w->remapper = std::make_shared<gcpad::GamepadInputRemapper>();
    if (!w->remapper) { delete w; return nullptr; }
    return static_cast<GCPadRemapperHandle>(w);
}

void gcpad_remapper_destroy(GCPadRemapperHandle remapper) {
    auto* w = to_remapper_wrapper(remapper);
    delete w;
}

void gcpad_remapper_map_button_to_key(GCPadRemapperHandle remapper, int button, uint16_t virtual_key) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->mapButtonToKey(static_cast<gcpad::Button>(button), virtual_key);
}

void gcpad_remapper_map_button_to_mouse(GCPadRemapperHandle remapper, int button, int mouse_button) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->mapButtonToMouseButton(
        static_cast<gcpad::Button>(button),
        static_cast<gcpad::MouseButton>(mouse_button)
    );
}

void gcpad_remapper_map_button_to_wheel(GCPadRemapperHandle remapper, int button, int delta) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->mapButtonToWheel(static_cast<gcpad::Button>(button), delta);
}

void gcpad_remapper_map_axis_to_mouse(GCPadRemapperHandle remapper, int axis, float sensitivity, float deadzone, int invert, float curve) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->mapAxisToMouse(
        static_cast<gcpad::Axis>(axis),
        sensitivity, deadzone, invert != 0, curve
    );
}

void gcpad_remapper_map_axis_to_key(GCPadRemapperHandle remapper, int axis, uint16_t virtual_key, float threshold, int negative_direction) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->mapAxisToKey(
        static_cast<gcpad::Axis>(axis),
        virtual_key, threshold, negative_direction != 0
    );
}

void gcpad_remapper_map_axis_to_mouse_button(GCPadRemapperHandle remapper, int axis, int mouse_button, float threshold) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->mapAxisToMouseButton(
        static_cast<gcpad::Axis>(axis),
        static_cast<gcpad::MouseButton>(mouse_button),
        threshold
    );
}

void gcpad_remapper_map_axis_to_wheel(GCPadRemapperHandle remapper, int axis, int delta, float deadzone, int invert, float tick_rate) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->mapAxisToWheel(
        static_cast<gcpad::Axis>(axis),
        delta, deadzone, invert != 0, tick_rate
    );
}

void gcpad_remapper_clear_all(GCPadRemapperHandle remapper) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->clearAllButtonMappings();
    w->remapper->clearAllAxisMappings();
}

int gcpad_remapper_send_input(GCPadRemapperHandle remapper, GCPadStateC* current_state, GCPadStateC* previous_state) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return 0;

    gcpad::GamepadState cur, prev;
    for (int i = 0; i < GCPAD_BUTTON_COUNT; ++i) {
        cur.buttons[i] = current_state->buttons[i] != 0;
    }
    for (int i = 0; i < GCPAD_AXIS_COUNT; ++i) {
        cur.axes[i] = current_state->axes[i];
    }

    if (previous_state) {
        for (int i = 0; i < GCPAD_BUTTON_COUNT; ++i) {
            prev.buttons[i] = previous_state->buttons[i] != 0;
        }
        for (int i = 0; i < GCPAD_AXIS_COUNT; ++i) {
            prev.axes[i] = previous_state->axes[i];
        }
    }

    return w->remapper->sendInput(cur, prev) ? 1 : 0;
}

void gcpad_remapper_reset_state(GCPadRemapperHandle remapper) {
    auto* w = to_remapper_wrapper(remapper);
    if (!w || !w->remapper) return;
    w->remapper->resetState();
    w->previous_state = {};
}

} /* extern "C" */