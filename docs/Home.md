# GCPad API Documentation

GCPad is a cross-platform gamepad API designed to provide a unified interface for gamepad input across multiple controller types. Originally developed for UnionCrax.Direct, it enables controllers to work without Steam or other middleware.

## Features

- **Multi-controller support**: Xbox, PlayStation DualShock 4/DualSense, Nintendo Pro Controller
- **Input remapping**: Map gamepad buttons/axes to keyboard keys and mouse input
- **Cross-platform**: Windows and Linux support
- **Zero dependencies**: Bundled SDL2 for Windows
- **HID integration**: Works with XInput, DirectInput, and native HID

## Supported Controllers

| Controller | Status | Notes |
|------------|--------|-------|
| Xbox 360/One/Series X | Tested | XInput |
| PlayStation DualShock 4 | Tested | Native HID |
| PlayStation DualSense | Tested | Native HID |
| Nintendo Switch Pro Controller | Tested | HID |
| Generic HID Gamepads | Supported | May need testing |

## Table of Contents

1. [Building GCPad](#building-gcpad)
2. [Quick Start](#quick-start)
3. [Using the Core Library](#using-the-core-library)
4. [Input Remapping](#input-remapping)
5. [Implementing a Frontend](#implementing-a-frontend)
6. [Adding New Controller Support](#adding-new-controller-support)
7. [API Reference](#api-reference)

---

## Building GCPad

### Prerequisites

- **Windows**: Visual Studio 2022, CMake 3.15+
- **Linux**: CMake 3.15+, X11 development libraries, SDL2

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/vee/GCPad_API.git
cd GCPad_API

# Configure (Windows)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Configure (Linux)
cmake -B build

# Build
cmake --build build --config Release
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `GCPAD_BUILD_FRONTEND` | ON (Windows) | Build console frontend |
| `GCPAD_BUILD_FRONTEND_GUI` | ON | Build GUI frontend |
| `GCPAD_BUILD_REMAP` | ON | Build input remapper |

---

## Quick Start

### Basic Gamepad Input

```cpp
#include "GamepadManager.h"
#include "gcpad.h"
#include <iostream>

int main() {
    // Create the manager
    auto manager = gcpad::createGamepadManager();
    if (!manager->initialize()) {
        std::cerr << "Failed to initialize: " << manager->getLastError() << "\n";
        return 1;
    }

    // Main loop
    while (true) {
        manager->updateAll();

        // Check each gamepad
        for (int i = 0; i < manager->getMaxGamepads(); i++) {
            auto* gamepad = manager->getGamepad(i);
            if (gamepad && gamepad->isConnected()) {
                const auto& state = gamepad->getState();
                
                // Read buttons
                if (state.isButtonPressed(gcpad::Button::A)) {
                    std::cout << "A pressed!\n";
                }
                
                // Read axes (-1.0 to 1.0)
                float leftX = state.getAxis(gcpad::Axis::LeftX);
                float leftY = state.getAxis(gcpad::Axis::LeftY);
            }
        }
    }

    manager->shutdown();
    return 0;
}
```

---

## Using the Core Library

### GamepadManager

The `GamepadManager` is the main entry point for all gamepad operations:

```cpp
#include "GamepadManager.h"

// Create manager
auto manager = gcpad::createGamepadManager();
manager->initialize();

// Get connected gamepads
int count = manager->getConnectedGamepadCount();
auto indices = manager->getConnectedGamepadIndices();

// Access a specific gamepad
auto* gamepad = manager->getGamepad(0);
if (gamepad->isConnected()) {
    const auto& state = gamepad->getState();
    // ...
}

// Set up callbacks for hotplugging
manager->setGamepadConnectedCallback([](int index) {
    std::cout << "Gamepad connected: " << index << "\n";
});

manager->setGamepadDisconnectedCallback([](int index) {
    std::cout << "Gamepad disconnected: " << index << "\n";
});

// Cleanup
manager->shutdown();
```

### GamepadState

The `GamepadState` struct contains all input data:

```cpp
const auto& state = gamepad->getState();

// Boolean buttons
state.isButtonPressed(gcpad::Button::A);
state.isButtonPressed(gcpad::Button::B);
state.isButtonPressed(gcpad::Button::X);
state.isButtonPressed(gcpad::Button::Y);

// Analog axes (-1.0 to 1.0)
float leftX = state.getAxis(gcpad::Axis::LeftX);
float leftY = state.getAxis(gcpad::Axis::LeftY);
float rightX = state.getAxis(gcpad::Axis::RightX);
float rightY = state.getAxis(gcpad::Axis::RightY);

// Triggers (0.0 to 1.0)
float leftTrigger = state.getAxis(gcpad::Axis::LeftTrigger);
float rightTrigger = state.getAxis(gcpad::Axis::RightTrigger);

// Motion sensors (if available)
float gyroX = state.gyro.x;
float accelY = state.accel.y;

// Touchpad (DualSense/DS4)
if (state.touchpad[0].active) {
    uint16_t x = state.touchpad[0].x;
    uint16_t y = state.touchpad[0].y;
}

// Battery (if available)
float battery = state.battery_level;  // 0.0 to 1.0
bool charging = state.is_charging;
```

### Controller Output

Control LED colors and rumble:

```cpp
// Set LED color
gcpad::Color color(255, 0, 0);  // Red
gamepad->setLED(color);

// Set rumble
gcpad::Rumble rumble(128, 200);  // left_motor, right_motor
gamepad->setRumble(rumble);

// DualSense trigger effects
gcpad::TriggerEffect effect = gcpad::TriggerEffect::Resistance(50, 128);
gamepad->setTriggerEffect(true, effect);  // Right trigger
```

---

## Input Remapping

The `GamepadInputRemapper` translates gamepad input into keyboard/mouse events that games can understand.

### Setup

```cpp
#include "gamepad_input_remapper.h"

// Create remapper
gcpad::GamepadInputRemapper remapper;

// Map gamepad buttons to keyboard keys
remapper.mapButtonToKey(gcpad::Button::A, VK_A);      // A key
remapper.mapButtonToKey(gcpad::Button::B, VK_ESCAPE); // Escape
remapper.mapButtonToKey(gcpad::Button::X, VK_SPACE);   // Space

// Map buttons to mouse buttons
remapper.mapButtonToMouseButton(gcpad::Button::X, gcpad::MouseButton::Left);
```

### Axis to Mouse Movement

Map analog sticks to mouse cursor movement:

```cpp
// Left stick -> mouse cursor
remapper.mapAxisToMouse(gcpad::Axis::LeftX, 
    15.0f,   // sensitivity (pixels per frame at full deflection)
    0.15f,   // deadzone
    false,   // invert
    2.0f);   // curve (higher = more precision at low deflection)

// Right stick -> mouse cursor (optional)
remapper.mapAxisToMouse(gcpad::Axis::RightX, 15.0f, 0.15f, false, 2.0f);
remapper.mapAxisToMouse(gcpad::Axis::RightY, 15.0f, 0.15f, false, 2.0f);
```

### Axis to Key (Threshold-based)

Map axes to keys when they exceed a threshold:

```cpp
// Triggers act as buttons
remapper.mapAxisToKey(gcpad::Axis::LeftTrigger, VK_LBUTTON, 0.5f);
remapper.mapAxisToKey(gcpad::Axis::RightTrigger, VK_RBUTTON, 0.5f);

// Stick directions
remapper.mapAxisToKey(gcpad::Axis::LeftX, VK_LEFT, 0.8f, false);   // positive = right
remapper.mapAxisToKey(gcpad::Axis::LeftX, VK_LEFT, 0.8f, true);    // negative = left
```

### Axis to Mouse Button

```cpp
// Left trigger -> left click
remapper.mapAxisToMouseButton(gcpad::Axis::LeftTrigger, 
    gcpad::MouseButton::Left, 0.5f);
```

### Axis to Scroll Wheel

```cpp
// Right stick Y -> scroll
remapper.mapAxisToWheel(gcpad::Axis::RightY,
    120,     // delta per tick (WHEEL_DELTA)
    0.2f,    // deadzone
    false,   // invert
    0.15f);  // tick rate
```

### Sending Input

The `sendInput()` method injects the remapped events into the OS:

```cpp
gcpad::GamepadState current = gamepad->getState();
gcpad::GamepadState previous;  // Store previous state

// In your game loop:
void onGamepadInput() {
    previous = current;
    current = gamepad->getState();
    
    // Generate AND send events
    bool success = remapper.sendInput(current, previous);
}
```

Or use `remap()` to get events without sending:

```cpp
auto events = remapper.remap(current, previous);
// Process events yourself...
```

---

## Implementing a Frontend

### Project Setup

Add GCPad to your CMake project:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyFrontend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Add GCPad_Lib
add_subdirectory(GCPad_Lib)

# Add GCPad_Remap (optional)
add_subdirectory(GCPad_Remap)

# Your executable
add_executable(myfrontend src/main.cpp)
target_link_libraries(myfrontend PRIVATE gcpad gcpad_remap)
```

### Include Paths

Add these directories to your include path:
- `GCPad_Lib/include/`
- `GCPad_Remap/include/`

### Full Example

```cpp
#include "GamepadManager.h"
#include "gamepad_input_remapper.h"
#include <iostream>
#include <memory>

int main() {
    // Initialize manager
    auto manager = gcpad::createGamepadManager();
    if (!manager->initialize()) {
        std::cerr << "Init failed: " << manager->getLastError() << "\n";
        return 1;
    }

    // Create and configure remapper
    gcpad::GamepadInputRemapper remapper;
    
    // Map buttons to keys
    remapper.mapButtonToKey(gcpad::Button::A, 0x41);  // A key
    remapper.mapButtonToKey(gcpad::Button::B, 0x53); // S key
    remapper.mapButtonToKey(gcpad::Button::X, 0x44); // D key
    remapper.mapButtonToKey(gcpad::Button::Y, 0x46); // F key
    
    // Map left stick to mouse
    remapper.mapAxisToMouse(gcpad::Axis::LeftX, 15.0f, 0.15f, false, 2.0f);
    remapper.mapAxisToMouse(gcpad::Axis::LeftY, 15.0f, 0.15f, false, 2.0f);
    
    // Map triggers to mouse buttons
    remapper.mapAxisToMouseButton(gcpad::Axis::LeftTrigger, 
        gcpad::MouseButton::Left, 0.5f);
    remapper.mapAxisToMouseButton(gcpad::Axis::RightTrigger, 
        gcpad::MouseButton::Right, 0.5f);

    // Main loop
    gcpad::GamepadState previous;
    
    while (true) {
        manager->updateAll();
        
        for (int i = 0; i < manager->getMaxGamepads(); i++) {
            auto* gamepad = manager->getGamepad(i);
            if (gamepad && gamepad->isConnected()) {
                const auto& current = gamepad->getState();
                
                // Send remapped input to OS
                remapper.sendInput(current, previous);
                
                previous = current;
            }
        }
    }

    manager->shutdown();
    return 0;
}
```

---

## Adding New Controller Support

### Controller Backend Structure

New controllers are added by implementing the `GamepadDevice` interface:

```cpp
#include "GamepadManager.h"

class MyController : public gcpad::GamepadDevice {
public:
    MyController(int index, void* handle);
    
    int getIndex() const override { return index_; }
    std::string getName() const override { return name_; }
    std::string getSerialNumber() const override { return serial_; }
    
    const gcpad::GamepadState& getState() const override { return state_; }
    bool updateState() override;
    
    bool setLED(const gcpad::Color& color) override;
    bool setRumble(const gcpad::Rumble& rumble) override;
    
    void setRemapper(std::shared_ptr<gcpad::Remapper> remapper) override;
    gcpad::GamepadState getRemappedState() const override;
    
    bool isConnected() const override { return connected_; }

private:
    int index_;
    std::string name_;
    std::string serial_;
    gcpad::GamepadState state_;
    bool connected_;
    void* handle_;
};
```

### Implementation Requirements

1. **State polling**: Update `state_` with current button/axis values in `updateState()`
2. **Button mapping**: Map your controller's buttons to the standard `Button` enum
3. **Axis mapping**: Normalize all axes to -1.0 to 1.0 range (triggers to 0.0 to 1.0)
4. **Output support**: Implement `setLED()`, `setRumble()`, etc. if supported
5. **Thread safety**: Ensure state updates are thread-safe if called from multiple threads

### Integration

Register your controller in `ControllerLibrary`:

```cpp
void ControllerLibrary::registerDevice(std::unique_ptr<GamepadDevice> device) {
    devices_[device->getIndex()] = std::move(device);
}
```

---

## API Reference

### Enums

#### `gcpad::Button`
```cpp
enum class Button {
    A, B, X, Y,
    Start, Select, Guide,
    L1, R1, L2, R2, L3, R3,
    DPad_Up, DPad_Down, DPad_Left, DPad_Right,
    Touchpad,
    COUNT
};
```

#### `gcpad::Axis`
```cpp
enum class Axis {
    LeftX, LeftY, RightX, RightY,
    LeftTrigger, RightTrigger,
    COUNT
};
```

#### `gcpad::MouseButton`
```cpp
enum class MouseButton {
    Left, Right, Middle
};
```

### Key Structures

#### `gcpad::GamepadState`
```cpp
struct GamepadState {
    std::array<bool, static_cast<size_t>(Button::COUNT)> buttons;
    std::array<float, static_cast<size_t>(Axis::COUNT)> axes;
    
    // Motion sensors
    struct { float x, y, z; } gyro, accel;
    
    // Touchpad
    std::array<TouchPoint, 2> touchpad;
    
    // Status
    float battery_level;
    bool is_charging;
    bool is_connected;
    
    // Helpers
    bool isButtonPressed(Button btn) const;
    float getAxis(Axis axis) const;
};
```

#### `gcpad::GamepadInputEvent`
```cpp
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
```

### Key Codes

Use Windows virtual key codes for keyboard mapping. Common codes:

| Key | Code |
|-----|------|
| A-Z | 0x41-0x5A |
| 0-9 | 0x30-0x39 |
| Space | 0x20 |
| Enter | 0x0D |
| Escape | 0x1B |
| Arrow Keys | VK_LEFT (0x25), VK_RIGHT (0x27), etc. |
| Mouse Left | VK_LBUTTON (0x01) |
| Mouse Right | VK_RBUTTON (0x02) |

---

## Troubleshooting

### Controller Not Detected

1. Check USB connection
2. Verify the controller is supported
3. Check for driver issues
4. Try a different USB port

### Input Not Working

1. Ensure `manager->updateAll()` is called every frame
2. Verify the gamepad is connected (`isConnected()`)
3. Check button/axis mappings are correct
4. For remapping, ensure `sendInput()` is called

### Rumble/LED Not Working

1. Check if controller supports the feature
2. Verify the correct protocol is being used
3. Some features require specific drivers (e.g., DS4 on Windows)

---

