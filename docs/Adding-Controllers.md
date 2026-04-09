# Adding Controller Support

Add support for new gamepad controllers by implementing the `GamepadDevice` interface.

## Architecture

GCPad uses a plugin-style architecture for controller support:

```
GamepadManager
    ├── XboxDevice (XInput)
    ├── PsDevice (DualShock 4/DualSense)
    ├── NintendoDevice (Pro Controller)
    └── ...
```

Each device inherits from `GamepadDevice` base class.

## Adding a New Controller

### Step 1: Create Device Class

Create a new header file in `GCPad_Lib/internal/`:

```cpp
// internal/my_controller.h
#pragma once

#include "GamepadManager.h"
#include <string>

class MyController : public gcpad::GamepadDevice {
public:
    MyController(int index, void* nativeHandle);
    ~MyController() override;
    
    // Device information
    int getIndex() const override { return index_; }
    std::string getName() const override { return name_; }
    std::string getSerialNumber() const override { return serial_; }
    
    // State access
    const gcpad::GamepadState& getState() const override { return state_; }
    bool updateState() override;
    
    // Output
    bool setLED(const gcpad::Color& color) override;
    bool setRumble(const gcpad::Rumble& rumble) override;
    
    // Remapping
    void setRemapper(std::shared_ptr<gcpad::Remapper> remapper) override;
    gcpad::GamepadState getRemappedState() const override;
    
    // Connection
    bool isConnected() const override { return connected_; }

private:
    void parseInputReport(const uint8_t* data, size_t size);
    void mapButtons(const uint8_t* data);
    void mapAxes(const uint8_t* data);
    
    int index_;
    std::string name_;
    std::string serial_;
    gcpad::GamepadState state_;
    gcpad::GamepadState remapped_state_;
    bool connected_;
    void* handle_;
    std::shared_ptr<gcpad::Remapper> remapper_;
};
```

### Step 2: Implement State Polling

The most critical method is `updateState()`:

```cpp
bool MyController::updateState() {
    if (!connected_) return false;
    
    // Get input report from hardware
    uint8_t buffer[64];
    size_t bytesRead = readFromDevice(handle_, buffer, sizeof(buffer));
    if (bytesRead == 0) return false;
    
    // Parse and update state
    parseInputReport(buffer, bytesRead);
    
    // Apply remapper if set
    if (remapper_) {
        remapped_state_ = remapper_->apply(state_);
    }
    
    state_.setTimestamp(std::chrono::steady_clock::now());
    return true;
}
```

### Step 3: Map Controller Buttons to Standard Enum

```cpp
void MyController::mapButtons(const uint8_t* data) {
    // Example mapping - adjust based on your controller's report format
    state_.buttons[static_cast<size_t>(gcpad::Button::A)]     = (data[0] & 0x01) != 0;
    state_.buttons[static_cast<size_t>(gcpad::Button::B)]     = (data[0] & 0x02) != 0;
    state_.buttons[static_cast<size_t>(gcpad::Button::X)]     = (data[0] & 0x04) != 0;
    state_.buttons[static_cast<size_t>(gcpad::Button::Y)]     = (data[0] & 0x08) != 0;
    state_.buttons[static_cast<size_t>(gcpad::Button::L1)]    = (data[0] & 0x10) != 0;
    state_.buttons[static_cast<size_t>(gcpad::Button::R1)]    = (data[0] & 0x20) != 0;
    // ... more mappings
}
```

### Step 4: Normalize Axes

Axes must be normalized to -1.0 to 1.0 (or 0.0 to 1.0 for triggers):

```cpp
void MyController::mapAxes(const uint8_t* data) {
    // Left stick (16-bit signed, centered at 0)
    int16_t rawLX = (int16_t)(data[1] | (data[2] << 8));
    state_.axes[static_cast<size_t>(gcpad::Axis::LeftX)] = rawLX / 32768.0f;
    
    int16_t rawLY = (int16_t)(data[3] | (data[4] << 8));
    state_.axes[static_cast<size_t>(gcpad::Axis::LeftY)] = rawLY / 32768.0f;
    
    // Triggers (8-bit unsigned, 0-255 -> 0.0-1.0)
    state_.axes[static_cast<size_t>(gcpad::Axis::LeftTrigger)] = data[5] / 255.0f;
    state_.axes[static_cast<size_t>(gcpad::Axis::RightTrigger)] = data[6] / 255.0f;
}
```

### Step 5: Register in Controller Library

```cpp
// controller_library.cpp
#include "my_controller.h"

void ControllerLibrary::detectControllers() {
    // Your detection logic
    std::vector<ControllerInfo> found = enumerateHidDevices();
    
    for (const auto& info : found) {
        if (info.vendor_id == YOUR_VENDOR_ID && info.product_id == YOUR_PRODUCT_ID) {
            auto controller = std::make_unique<MyController>(info.index, info.handle);
            registerDevice(std::move(controller));
        }
    }
}
```

## Report Format Examples

### Xbox Controller (XInput)

```
Byte 0: Buttons (bitmask)
  - 0x01: D-pad Up
  - 0x02: D-pad Down
  - 0x04: D-pad Left
  - 0x08: D-pad Right
  - 0x10: Start
  - 0x20: Select
  - 0x40: L3
  - 0x80: R3
Byte 1: Buttons 2
  - 0x01: L1
  - 0x02: R1
  - 0x04: L2 (analog)
  - 0x08: R2 (analog)
  - 0x10: A
  - 0x20: B
  - 0x40: X
  - 0x80: Y
Bytes 2-3: Left stick X (int16)
Bytes 4-5: Left stick Y (int16)
Bytes 6-7: Right stick X (int16)
Bytes 8-9: Right stick Y (int16)
Byte 10: Left trigger (uint8)
Byte 11: Right trigger (uint8)
```

### DualShock 4 (HID)

Standard HID gamepad report (varies by firmware):
- Buttons in first bytes
- Axes as 16-bit signed values
- Gyro/accel in separate reports

### DualSense (HID)

Uses USB HID with more complex report structure:
- Standard buttons
- Adaptive triggers (separate feature report)
- Touchpad data
- Motion sensors

## Testing

1. Build with your new controller code
2. Connect the controller
3. Run the GUI frontend
4. Verify:
   - Controller is detected
   - All buttons map correctly
   - Axes have proper ranges
   - Rumble/LED work (if supported)

## Common Issues

### Controller Not Detected
- Check vendor/product ID matches
- Verify HID enumeration works
- Check for driver issues

### Wrong Button Mapping
- Inspect raw HID report with debugging
- Verify byte/bit positions

### Axes Not Centered
- Apply calibration offset
- Deadzone handling

### Rumble Not Working
- Verify correct feature report format
- Check controller supports it
