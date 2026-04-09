# API Reference

Complete API documentation for GCPad.

## Core Types

### Button Enum

```cpp
enum class Button {
    A,              // A / Cross button
    B,              // B / Circle button
    X,              // X / Square button
    Y,              // Y / Triangle button
    Start,          // Start / Options button
    Select,         // Select / Share button
    Guide,          // Guide / PS button
    L1,             // Left bumper
    R1,             // Right bumper
    L2,             // Left trigger (digital)
    R2,             // Right trigger (digital)
    L3,             // Left stick press
    R3,             // Right stick press
    DPad_Up,        // D-pad up
    DPad_Down,      // D-pad down
    DPad_Left,      // D-pad left
    DPad_Right,     // D-pad right
    Touchpad,       // Touchpad click
    COUNT           // Total count
};
```

### Axis Enum

```cpp
enum class Axis {
    LeftX,          // Left stick horizontal (-1 to 1)
    LeftY,          // Left stick vertical (-1 to 1)
    RightX,         // Right stick horizontal (-1 to 1)
    RightY,         // Right stick vertical (-1 to 1)
    LeftTrigger,    // Left trigger (0 to 1)
    RightTrigger,   // Right trigger (0 to 1)
    COUNT           // Total count
};
```

### MouseButton Enum

```cpp
enum class MouseButton {
    Left,   // Left mouse button
    Right,  // Right mouse button
    Middle  // Middle mouse button (scroll wheel click)
};
```

## Structures

### GamepadState

```cpp
struct GamepadState {
    // Digital buttons (index by Button enum)
    std::array<bool, static_cast<size_t>(Button::COUNT)> buttons;
    
    // Analog axes (index by Axis enum)
    // Sticks: -1.0 to 1.0, centered at 0.0
    // Triggers: 0.0 to 1.0
    std::array<float, static_cast<size_t>(Axis::COUNT)> axes;
    
    // Motion sensors (if available)
    struct { float x, y, z; } gyro;
    struct { float x, y, z; } accel;
    
    // Touchpad (DualSense/DS4)
    // x: 0-1919, y: 0-1079
    std::array<TouchPoint, 2> touchpad;
    
    // Status
    float battery_level;  // 0.0 to 1.0
    bool is_charging;
    bool is_connected;
    
    // Methods
    bool isButtonPressed(Button btn) const;
    float getAxis(Axis axis) const;
    std::chrono::steady_clock::time_point getTimestamp() const;
    void setTimestamp(std::chrono::steady_clock::time_point ts);
    void reset();
};
```

### TouchPoint

```cpp
struct TouchPoint {
    bool active;      // Is finger touching?
    uint16_t x;       // X position (0 to width-1)
    uint16_t y;       // Y position (0 to height-1)
};
```

### Color

```cpp
struct Color {
    uint8_t r;  // Red (0-255)
    uint8_t g;  // Green (0-255)
    uint8_t b;  // Blue (0-255)
    
    Color() = default;
    Color(uint8_t red, uint8_t green, uint8_t blue);
};
```

### Rumble

```cpp
struct Rumble {
    uint8_t left_motor;   // Low-frequency (0-255)
    uint8_t right_motor;  // High-frequency (0-255)
    
    Rumble() = default;
    Rumble(uint8_t left, uint8_t right);
};
```

### TriggerEffect (DualSense)

```cpp
struct TriggerEffect {
    enum class Mode : uint8_t {
        Off       = 0x00,  // No resistance
        Rigid     = 0x01,  // Constant resistance
        Pulse     = 0x02,  // Pulsing resistance
        SemiRigid = 0x25,  // Resistance that eases off
        Vibrating = 0x26,  // Vibrating effect
    };
    
    Mode mode;
    uint8_t start;    // Start position (0-255)
    uint8_t end;      // End position (0-255)
    uint8_t force;    // Force (0-255)
    uint8_t param1;   // Mode-specific
    uint8_t param2;   // Mode-specific
    
    static TriggerEffect Resistance(uint8_t start_pos, uint8_t force_val);
    static TriggerEffect Vibration(uint8_t start_pos, uint8_t amplitude, uint8_t frequency);
};
```

### GamepadInputEvent

```cpp
struct GamepadInputEvent {
    enum class Type {
        Keyboard,     // Keyboard key event
        MouseButton,  // Mouse button event
        MouseMove,    // Mouse movement event
        MouseWheel,   // Scroll wheel event
    } type;
    
    KeyboardEvent keyboard;
    MouseButtonEvent mouse_button;
    MouseMove mouse_move;
    MouseWheelEvent mouse_wheel;
};

struct KeyboardEvent {
    uint16_t virtual_key;  // Windows virtual key code
    uint16_t scan_code;    // Hardware scan code
    bool pressed;          // true = pressed, false = released
};

struct MouseButtonEvent {
    MouseButton button;
    bool pressed;
};

struct MouseMove {
    int dx;  // Delta X
    int dy;  // Delta Y
};

struct MouseWheelEvent {
    int delta;  // Scroll amount (positive = up)
};
```

## Classes

### GamepadManager

```cpp
class GamepadManager {
public:
    static std::unique_ptr<GamepadManager> create();
    virtual ~GamepadManager() = default;
    
    // Initialization
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    
    // Enumeration
    virtual int getMaxGamepads() const = 0;
    virtual int getConnectedGamepadCount() const = 0;
    virtual std::vector<int> getConnectedGamepadIndices() const = 0;
    
    // Access
    virtual GamepadDevice* getGamepad(int index) = 0;
    virtual const GamepadDevice* getGamepad(int index) const = 0;
    
    // Callbacks
    using GamepadConnectedCallback = std::function<void(int index)>;
    using GamepadDisconnectedCallback = std::function<void(int index)>;
    
    virtual void setGamepadConnectedCallback(GamepadConnectedCallback callback) = 0;
    virtual void setGamepadDisconnectedCallback(GamepadDisconnectedCallback callback) = 0;
    
    // Update
    virtual void updateAll() = 0;
    
    // Remapper
    virtual void setRemapper(std::shared_ptr<Remapper> remapper) = 0;
    virtual std::shared_ptr<Remapper> getRemapper() const = 0;
    
    // Error
    virtual std::string getLastError() const = 0;
};
```

### GamepadDevice

```cpp
class GamepadDevice {
public:
    virtual ~GamepadDevice() = default;
    
    // Info
    virtual int getIndex() const = 0;
    virtual std::string getName() const = 0;
    virtual std::string getSerialNumber() const = 0;
    
    // State
    virtual const GamepadState& getState() const = 0;
    virtual bool updateState() = 0;
    
    // Output
    virtual bool setLED(const Color& color) = 0;
    virtual bool setRumble(const Rumble& rumble) = 0;
    virtual bool setTriggerEffect(bool right_trigger, const TriggerEffect& effect) = 0;
    virtual bool setPlayerLEDs(uint8_t led_mask) = 0;
    
    // Remapping
    virtual void setRemapper(std::shared_ptr<Remapper> remapper) = 0;
    virtual GamepadState getRemappedState() const = 0;
    
    // Status
    virtual bool isConnected() const = 0;
};
```

### GamepadInputRemapper

```cpp
struct GamepadInputRemapper {
    // Button mappings
    std::array<std::optional<uint16_t>, ...> button_to_key;
    std::array<std::optional<MouseButton>, ...> button_to_mouse;
    
    // Axis mappings
    std::array<AxisMouseMapping, ...> axis_to_mouse;
    std::array<std::optional<AxisKeyMapping>, ...> axis_to_key_positive;
    std::array<std::optional<AxisKeyMapping>, ...> axis_to_key_negative;
    std::array<std::optional<AxisMouseButtonMapping>, ...> axis_to_mouse_button;
    std::array<std::optional<AxisWheelMapping>, ...> axis_to_wheel;
    
    GamepadInputRemapper();
    
    // Button mapping
    void mapButtonToKey(Button button, uint16_t virtual_key);
    void mapButtonToMouseButton(Button button, MouseButton mouse_button);
    void clearButtonMapping(Button button);
    void clearAllButtonMappings();
    
    // Axis mapping
    void mapAxisToMouse(Axis axis, float sensitivity, float deadzone, bool invert, float curve);
    void mapAxisToKey(Axis axis, uint16_t virtual_key, float threshold, bool negative_direction);
    void mapAxisToMouseButton(Axis axis, MouseButton mouse_button, float threshold);
    void mapAxisToWheel(Axis axis, int delta, float deadzone, bool invert, float tick_rate);
    void clearAxisMapping(Axis axis);
    void clearAllAxisMappings();
    
    // Processing
    std::vector<GamepadInputEvent> remap(const GamepadState& current, const GamepadState& previous) const;
    bool sendInput(const GamepadState& current, const GamepadState& previous);
    void resetState();
    
    // Utility
    static std::string virtualKeyName(uint16_t vk);
};
```

## Helper Functions

```cpp
// Create manager
std::unique_ptr<GamepadManager> createGamepadManager();

// Get all HID devices
std::vector<HidDeviceInfo> getAllHidDevices();

// Convenience functions
bool initializeGamepadManager(GamepadManager* manager);
void shutdownGamepadManager(GamepadManager* manager);
int getConnectedGamepadCount(GamepadManager* manager);
GamepadDevice* getGamepad(GamepadManager* manager, int index);
const GamepadState& getGamepadState(GamepadDevice* gamepad);

// Remapper
void setGlobalRemapper(GamepadManager* manager, std::shared_ptr<Remapper> remapper);
std::shared_ptr<Remapper> getGlobalRemapper(GamepadManager* manager);

// Build info
BuildMetadata getBuildMetadata();
```

## Mapping Structures

### AxisMouseMapping

```cpp
struct AxisMouseMapping {
    bool enabled;
    float sensitivity;  // Pixels per frame
    float deadzone;     // 0.0 to 1.0
    bool invert;
    float curve;        // Response curve exponent
};
```

### AxisKeyMapping

```cpp
struct AxisKeyMapping {
    bool enabled;
    uint16_t virtual_key;
    float threshold;           // 0.0 to 1.0
    bool negative_direction;
};
```

### AxisMouseButtonMapping

```cpp
struct AxisMouseButtonMapping {
    bool enabled;
    MouseButton button;
    float threshold;  // 0.0 to 1.0
};
```

### AxisWheelMapping

```cpp
struct AxisWheelMapping {
    bool enabled;
    int delta_per_tick;   // Usually 120
    float deadzone;       // 0.0 to 1.0
    bool invert;
    float tick_rate;
};
```

## Key Codes (Windows)

| Key | Code | Key | Code |
|-----|------|-----|------|
| A | 0x41 | Numpad 0 | 0x60 |
| B | 0x42 | Numpad 1 | 0x61 |
| ... | ... | ... | ... |
| Z | 0x5A | Numpad 9 | 0x69 |
| 0 | 0x30 | Multiply | 0x6A |
| 1 | 0x31 | Add | 0x6B |
| ... | ... | ... | ... |
| 9 | 0x39 | F1 | 0x70 |
| Space | 0x20 | ... | ... |
| Enter | 0x0D | F12 | 0x7B |
| Escape | 0x1B | | |
| Left Arrow | 0x25 | | |
| Up Arrow | 0x26 | | |
| Right Arrow | 0x27 | | |
| Down Arrow | 0x28 | | |

See [Microsoft Virtual Key Codes](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes) for complete list.
