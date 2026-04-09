# Input Remapping

The GCPad remapper translates gamepad input into keyboard and mouse events, allowing controllers to work as keyboard/mouse input for any game or application.

## Overview

The `GamepadInputRemapper` class:
- Maps gamepad buttons to keyboard keys
- Maps gamepad buttons to mouse buttons
- Maps analog sticks to mouse cursor movement
- Maps triggers/axes to keyboard keys (threshold-based)
- Maps axes to mouse buttons (threshold-based)
- Maps axes to scroll wheel

## Basic Setup

```cpp
#include "gamepad_input_remapper.h"

gcpad::GamepadInputRemapper remapper;

// Map button to keyboard key
remapper.mapButtonToKey(gcpad::Button::A, VK_SPACE);  // A -> Space

// Map button to mouse button
remapper.mapButtonToMouseButton(gcpad::Button::X, gcpad::MouseButton::Left);
```

## Button Mapping

### Button to Key

```cpp
// Map face buttons to common keys
remapper.mapButtonToKey(gcpad::Button::A, VK_SPACE);    // A -> Space
remapper.mapButtonToKey(gcpad::Button::B, VK_ESCAPE);   // B -> Escape
remapper.mapButtonToKey(gcpad::Button::X, VK_CONTROL); // X -> Ctrl
remapper.mapButtonToKey(gcpad::Button::Y, VK_SHIFT);   // Y -> Shift

// D-pad to arrow keys
remapper.mapButtonToKey(gcpad::Button::DPad_Up, VK_UP);
remapper.mapButtonToKey(gcpad::Button::DPad_Down, VK_DOWN);
remapper.mapButtonToKey(gcpad::Button::DPad_Left, VK_LEFT);
remapper.mapButtonToKey(gcpad::Button::DPad_Right, VK_RIGHT);
```

### Button to Mouse

```cpp
remapper.mapButtonToMouseButton(gcpad::Button::A, gcpad::MouseButton::Left);
remapper.mapButtonToMouseButton(gcpad::Button::B, gcpad::MouseButton::Right);
remapper.mapButtonToMouseButton(gcpad::Button::X, gcpad::MouseButton::Middle);
```

## Axis to Mouse Movement

Map analog sticks to mouse cursor control:

```cpp
// Left stick -> mouse movement
remapper.mapAxisToMouse(
    gcpad::Axis::LeftX,     // axis to map
    15.0f,                 // sensitivity (pixels per frame at full deflection)
    0.15f,                 // deadzone (0.0-1.0)
    false,                 // invert X axis
    2.0f                   // curve (higher = more precision at low deflection)
);

remapper.mapAxisToMouse(gcpad::Axis::LeftY, 15.0f, 0.15f, false, 2.0f);

// Right stick (optional)
remapper.mapAxisToMouse(gcpad::Axis::RightX, 15.0f, 0.15f, false, 2.0f);
remapper.mapAxisToMouse(gcpad::Axis::RightY, 15.0f, 0.15f, false, 2.0f);
```

### Parameters

| Parameter | Description | Typical Values |
|-----------|-------------|----------------|
| sensitivity | Pixels moved per frame at full stick deflection | 10-30 |
| deadzone | Stick region (0-1) ignored as dead input | 0.1-0.2 |
| invert | Flip the axis direction | false |
| curve | Exponent for response curve (1=linear, 2=quadratic) | 1.5-2.5 |

## Axis to Key (Threshold-based)

Map axes to keys when they exceed a threshold:

```cpp
// Triggers as buttons (0.0-1.0 range)
remapper.mapAxisToKey(
    gcpad::Axis::LeftTrigger,   // axis
    VK_LBUTTON,                  // virtual key code
    0.5f                        // threshold (0.0-1.0)
);

remapper.mapAxisToKey(gcpad::Axis::RightTrigger, VK_RBUTTON, 0.5f);

// Stick directions (use negative_direction parameter)
remapper.mapAxisToKey(gcpad::Axis::LeftX, VK_LEFT, 0.8f, false);  // positive = right
remapper.mapAxisToKey(gcpad::Axis::LeftX, VK_RIGHT, 0.8f, true);   // negative = left
```

## Axis to Mouse Button

```cpp
// Left trigger -> left click
remapper.mapAxisToMouseButton(
    gcpad::Axis::LeftTrigger,
    gcpad::MouseButton::Left,
    0.5f  // threshold
);

// Right trigger -> right click
remapper.mapAxisToMouseButton(gcpad::Axis::RightTrigger, gcpad::MouseButton::Right, 0.5f);
```

## Axis to Scroll Wheel

```cpp
// Right stick Y -> vertical scroll
remapper.mapAxisToWheel(
    gcpad::Axis::RightY,
    120,     // delta per tick (WHEEL_DELTA)
    0.2f,    // deadzone
    false,   // invert
    0.15f    // tick rate
);

// Left stick X -> horizontal scroll
remapper.mapAxisToWheel(gcpad::Axis::LeftX, 120, 0.2f, false, 0.15f);
```

## Sending Remapped Input

### Auto-send (Recommended)

```cpp
gcpad::GamepadState current;
gcpad::GamepadState previous;

void onGamepadFrame() {
    previous = current;
    current = gamepad->getState();
    
    // Generates AND sends events to OS
    remapper.sendInput(current, previous);
}
```

### Manual Event Processing

```cpp
void onGamepadFrame() {
    previous = current;
    current = gamepad->getState();
    
    // Just generate events (doesn't send)
    auto events = remapper.remap(current, previous);
    
    // Process events yourself
    for (const auto& event : events) {
        switch (event.type) {
            case gcpad::GamepadInputEvent::Type::Keyboard:
                // Handle keyboard event
                break;
            case gcpad::GamepadInputEvent::Type::MouseButton:
                // Handle mouse button event
                break;
            // ... other types
        }
    }
}
```

## Clearing Mappings

```cpp
// Clear single button mapping
remapper.clearButtonMapping(gcpad::Button::A);

// Clear all button mappings
remapper.clearAllButtonMappings();

// Clear single axis mapping
remapper.clearAxisMapping(gcpad::Axis::LeftX);

// Clear all axis mappings
remapper.clearAllAxisMappings();

// Reset all state (call when switching profiles)
remapper.resetState();
```

## Common Key Codes

| Key | Virtual Code |
|-----|-------------|
| A-Z | 0x41-0x5A |
| 0-9 | 0x30-0x39 |
| Space | 0x20 |
| Enter | 0x0D |
| Escape | 0x1B |
| Backspace | 0x08 |
| Tab | 0x09 |
| Left Arrow | 0x25 |
| Up Arrow | 0x26 |
| Right Arrow | 0x27 |
| Down Arrow | 0x28 |
| Home | 0x24 |
| End | 0x23 |
| Page Up | 0x21 |
| Page Down | 0x22 |
| Insert | 0x2D |
| Delete | 0x2E |
| Left Mouse | 0x01 |
| Right Mouse | 0x02 |
| Middle Mouse | 0x04 |

## Platform Differences

### Windows
- Uses `SendInput` API
- Supports both virtual key codes and scan codes
- Extended key flags handled automatically

### Linux
- Uses X11 XTest extension
- Virtual key codes mapped to X11 keycodes
- Requires X server access
