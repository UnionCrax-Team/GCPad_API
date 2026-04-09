# Core Library

The core GCPad library provides gamepad detection, state polling, and device management.

## GamepadManager

The `GamepadManager` is the main entry point:

```cpp
#include "GamepadManager.h"

// Create manager
auto manager = gcpad::createGamepadManager();
manager->initialize();

// Enumerate devices
int count = manager->getConnectedGamepadCount();
auto indices = manager->getConnectedGamepadIndices();

// Access device
auto* gamepad = manager->getGamepad(0);

// Call every frame
manager->updateAll();

// Cleanup
manager->shutdown();
```

## GamepadState

The `GamepadState` struct contains all input data:

### Buttons

```cpp
const auto& state = gamepad->getState();

// Check individual buttons
if (state.isButtonPressed(gcpad::Button::A)) {
    // A pressed
}

// Or access raw array
bool aPressed = state.buttons[static_cast<size_t>(gcpad::Button::A)];
```

### Axes

```cpp
// Analog sticks: -1.0 to 1.0, centered at 0.0
float leftX = state.getAxis(gcpad::Axis::LeftX);
float leftY = state.getAxis(gcpad::Axis::LeftY);
float rightX = state.getAxis(gcpad::Axis::RightX);
float rightY = state.getAxis(gcpad::Axis::RightY);

// Triggers: 0.0 to 1.0
float leftTrigger = state.getAxis(gcpad::Axis::LeftTrigger);
float rightTrigger = state.getAxis(gcpad::Axis::RightTrigger);
```

### Motion Sensors

```cpp
// Gyroscope (degrees per second)
float gyroX = state.gyro.x;
float gyroY = state.gyro.y;
float gyroZ = state.gyro.z;

// Accelerometer (meters per second squared)
float accelX = state.accel.x;
float accelY = state.accel.y;
float accelZ = state.accel.z;
```

### Touchpad (DualSense/DS4)

```cpp
// First finger
if (state.touchpad[0].active) {
    uint16_t x = state.touchpad[0].x;  // 0-1919
    uint16_t y = state.touchpad[0].y;  // 0-1079
}

// Second finger (if multi-touch)
if (state.touchpad[1].active) {
    // ...
}
```

### Status

```cpp
// Battery (0.0 to 1.0)
float battery = state.battery_level;
bool charging = state.is_charging;

// Connection status
bool connected = state.is_connected;

// Timestamp
auto timestamp = state.getTimestamp();
```

## Hotplug Callbacks

```cpp
manager->setGamepadConnectedCallback([](int index) {
    std::cout << "Connected: " << index << "\n";
});

manager->setGamepadDisconnectedCallback([](int index) {
    std::cout << "Disconnected: " << index << "\n";
});
```

## Controller Output

### Set LED Color

```cpp
gcpad::Color color(255, 0, 128);  // RGB
gamepad->setLED(color);
```

### Set Rumble

```cpp
gcpad::Rumble rumble(128, 200);  // left_motor (0-255), right_motor (0-255)
gamepad->setRumble(rumble);
```

### DualSense Trigger Effects

```cpp
// Constant resistance
auto effect = gcpad::TriggerEffect::Resistance(50, 128);  // start_position, force
gamepad->setTriggerEffect(true, effect);  // right trigger

// Vibrating
auto effect2 = gcpad::TriggerEffect::Vibration(30, 100, 50);  // start, amplitude, frequency
gamepad->setTriggerEffect(false, effect2);  // left trigger
```

## Button and Axis Enums

### Button

| Enum | Description |
|------|-------------|
| `Button::A` | A / Cross button |
| `Button::B` | B / Circle button |
| `Button::X` | X / Square button |
| `Button::Y` | Y / Triangle button |
| `Button::Start` | Start / Options button |
| `Button::Select` | Select / Share button |
| `Button::Guide` | Guide / PS button |
| `Button::L1` | Left bumper |
| `Button::R1` | Right bumper |
| `Button::L2` | Left trigger (digital) |
| `Button::R2` | Right trigger (digital) |
| `Button::L3` | Left stick press |
| `Button::R3` | Right stick press |
| `Button::DPad_*` | D-pad directions |
| `Button::Touchpad` | Touchpad press |

### Axis

| Enum | Description |
|------|-------------|
| `Axis::LeftX` | Left stick horizontal |
| `Axis::LeftY` | Left stick vertical |
| `Axis::RightX` | Right stick horizontal |
| `Axis::RightY` | Right stick vertical |
| `Axis::LeftTrigger` | Left trigger (0-1) |
| `Axis::RightTrigger` | Right trigger (0-1) |
