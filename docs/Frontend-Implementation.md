# Implementing a Frontend

Add GCPad to your own application with these integration steps.

## CMake Integration

### Option 1: Include GCPad as Subdirectory

Add to your `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add GCPad as subdirectory (point to your GCPad location)
add_subdirectory(GCPad_Lib)
add_subdirectory(GCPad_Remap)

# Your executable
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE gcpad gcpad_remap)
```

### Option 2: Use Pre-built Libraries

```cmake
# Find GCPad (if installed as package)
find_package(gcpad REQUIRED)

# Or link directly to pre-built files
target_link_libraries(myapp PRIVATE gcpad gcpad_remap)
target_include_directories(myapp PRIVATE path/to/GCPad_Lib/include path/to/GCPad_Remap/include)
```

## Include Paths

Add these directories to your include paths:
- `GCPad_Lib/include/`
- `GCPad_Remap/include/`

## Minimal Example

```cpp
#include "GamepadManager.h"
#include "gamepad_input_remapper.h"
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>

int main() {
    // Create and initialize manager
    auto manager = gcpad::createGamepadManager();
    if (!manager->initialize()) {
        std::cerr << "Failed to initialize: " << manager->getLastError() << "\n";
        return 1;
    }

    // Create and configure remapper
    gcpad::GamepadInputRemapper remapper;
    
    // Map buttons to keys
    remapper.mapButtonToKey(gcpad::Button::A, 0x41);  // A
    remapper.mapButtonToKey(gcpad::Button::B, 0x53); // S
    remapper.mapButtonToKey(gcpad::Button::X, 0x44); // D
    remapper.mapButtonToKey(gcpad::Button::Y, 0x46); // F
    
    // Map left stick to mouse
    remapper.mapAxisToMouse(gcpad::Axis::LeftX, 15.0f, 0.15f, false, 2.0f);
    remapper.mapAxisToMouse(gcpad::Axis::LeftY, 15.0f, 0.15f, false, 2.0f);
    
    // Map triggers to mouse buttons
    remapper.mapAxisToMouseButton(gcpad::Axis::LeftTrigger, gcpad::MouseButton::Left, 0.5f);
    remapper.mapAxisToMouseButton(gcpad::Axis::RightTrigger, gcpad::MouseButton::Right, 0.5f);

    // Store previous state for delta calculations
    gcpad::GamepadState previous;

    std::cout << "Press Ctrl+C to exit\n";
    
    // Main loop
    while (true) {
        // Update all gamepads
        manager->updateAll();
        
        // Process each connected gamepad
        for (int i = 0; i < manager->getMaxGamepads(); i++) {
            auto* gamepad = manager->getGamepad(i);
            if (!gamepad || !gamepad->isConnected()) continue;
            
            const auto& current = gamepad->getState();
            
            // Display live input (for debugging)
            std::cout << "Gamepad " << i << ": ";
            for (int b = 0; b < static_cast<int>(gcpad::Button::COUNT); b++) {
                if (current.buttons[b]) {
                    std::cout << "[" << b << "] ";
                }
            }
            std::cout << "L:" << current.axes[0] << " R:" << current.axes[2] << "\r";
            
            // Send remapped input to OS
            bool success = remapper.sendInput(current, previous);
            if (!success) {
                std::cerr << "Failed to send input\n";
            }
            
            previous = current;
        }
        
        // Small delay to prevent hogging CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    manager->shutdown();
    return 0;
}
```

## Full Integration Example with Rumble

```cpp
#include "GamepadManager.h"
#include "gamepad_input_remapper.h"
#include <iostream>
#include <memory>
#include <atomic>

class MyGamepadApp {
public:
    MyGamepadApp() : manager_(gcpad::createGamepadManager()), running_(true) {}
    
    bool init() {
        if (!manager_->initialize()) {
            std::cerr << "Init failed: " << manager_->getLastError() << "\n";
            return false;
        }
        
        // Set up callbacks
        manager_->setGamepadConnectedCallback([this](int index) {
            std::cout << "Gamepad " << index << " connected\n";
            onGamepadConnected(index);
        });
        
        manager_->setGamepadDisconnectedCallback([this](int index) {
            std::cout << "Gamepad " << index << " disconnected\n";
        });
        
        return true;
    }
    
    void run() {
        gcpad::GamepadState previous;
        
        while (running_) {
            manager_->updateAll();
            
            for (int i = 0; i < manager_->getMaxGamepads(); i++) {
                auto* gamepad = manager_->getGamepad(i);
                if (!gamepad || !gamepad->isConnected()) continue;
                
                const auto& current = gamepad->getState();
                
                // Send remapped input
                remapper_.sendInput(current, previous);
                
                // Example: Rumble when A is pressed
                if (current.isButtonPressed(gcpad::Button::A) && 
                    !previous.isButtonPressed(gcpad::Button::A)) {
                    gamepad->setRumble(gcpad::Rumble(128, 200));
                }
                
                previous = current;
            }
        }
    }
    
    void shutdown() {
        running_ = false;
        manager_->shutdown();
    }
    
private:
    void onGamepadConnected(int index) {
        auto* gamepad = manager_->getGamepad(index);
        if (!gamepad) return;
        
        // Configure default mappings for this gamepad
        // (You could load from a config file)
        std::cout << "Configuring mappings for: " << gamepad->getName() << "\n";
    }
    
    std::unique_ptr<gcpad::GamepadManager> manager_;
    gcpad::GamepadInputRemapper remapper_;
    std::atomic<bool> running_;
};

int main() {
    MyGamepadApp app;
    if (!app.init()) return 1;
    
    app.run();
    app.shutdown();
    
    return 0;
}
```

## Windows-Specific Notes

### DLL Distribution

When distributing your application:

1. Copy `gcpad.dll` to your executable's directory
2. On Windows, also copy `SDL2.dll`
3. The remapper static library (`gcpad_remap.lib`) doesn't need distribution

### Unicode

GCPad uses UTF-8 internally. For Windows console output:

```cpp
// Set console to UTF-8 mode on Windows
#ifdef _WIN32
#include <windows.h>
SetConsoleOutputCP(CP_UTF8);
#endif
```

## Linux-Specific Notes

### X11 Permissions

For input injection, you need X11 permissions:

```bash
# Temporarily allow any X11 access
xhost +

# Or add to ~/.xauth (more secure)
```

### SDL2 on Linux

Install SDL2 system library:

```bash
# Ubuntu/Debian
sudo apt-get install libsdl2-dev

# Link with pkg-config
target_link_libraries(myapp PRIVATE PkgConfig::SDL2)
```
