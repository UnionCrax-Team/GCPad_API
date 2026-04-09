# Quick Start

Get up and running with GCPad in 5 minutes.

## Step 1: Build GCPad

Follow the [Building](Building.md) instructions for your platform, or use pre-built binaries from GitHub releases.

## Step 2: Run the GUI Frontend

```bash
# Windows
.\build\GCPad_Frontend_GUI\Release\GCPad_Frontend_GUI.exe

# Linux
./build/GCPad_Frontend_GUI/GCPad_Frontend_GUI
```

## Step 3: Connect a Controller

1. Connect your gamepad via USB or Bluetooth
2. The GUI should automatically detect it
3. You'll see live button/axis values in the display

## Step 4: Configure Input Remapping

In the GUI:
1. Go to the "Mapping" tab
2. Select a profile or create a new one
3. Map buttons to keys (e.g., A → Space)
4. Map sticks to mouse movement
5. Click "Apply" to enable remapping

## Basic Code Example

```cpp
#include "GamepadManager.h"
#include <iostream>

int main() {
    auto manager = gcpad::createGamepadManager();
    manager->initialize();

    while (true) {
        manager->updateAll();
        
        auto* gamepad = manager->getGamepad(0);
        if (gamepad && gamepad->isConnected()) {
            const auto& state = gamepad->getState();
            
            if (state.isButtonPressed(gcpad::Button::A)) {
                std::cout << "A pressed!\n";
            }
        }
    }

    manager->shutdown();
    return 0;
}
```

## Next Steps

- [Core Library](Core-Library.md) - Learn about the full API
- [Input Remapping](Input-Remapping.md) - Advanced remapping features
- [Frontend Implementation](Frontend-Implementation.md) - Add GCPad to your project
