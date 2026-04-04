// GCPad API Example
// Demonstrates: controller enumeration, reading inputs, and remapping to keyboard/mouse
//
// Build with: cmake -S examples -B build && cmake --build build --config Release
// Run:        build/Release/gcpad_example.exe

#include "GamepadManager.h"
#include "gamepad_input_remapper.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <WinUser.h>

static volatile bool running = true;

void signal_handler(int) {
    running = false;
}

void print_state(const gcpad::GamepadState& state, int index) {
    std::cout << "\r[Pad " << index << "] ";

    // Buttons
    if (state.isButtonPressed(gcpad::Button::A))      std::cout << "A ";
    if (state.isButtonPressed(gcpad::Button::B))      std::cout << "B ";
    if (state.isButtonPressed(gcpad::Button::X))      std::cout << "X ";
    if (state.isButtonPressed(gcpad::Button::Y))      std::cout << "Y ";
    if (state.isButtonPressed(gcpad::Button::L1))     std::cout << "L1 ";
    if (state.isButtonPressed(gcpad::Button::R1))     std::cout << "R1 ";
    if (state.isButtonPressed(gcpad::Button::L2))     std::cout << "L2 ";
    if (state.isButtonPressed(gcpad::Button::R2))     std::cout << "R2 ";
    if (state.isButtonPressed(gcpad::Button::Select)) std::cout << "SEL ";
    if (state.isButtonPressed(gcpad::Button::Start))  std::cout << "START ";
    if (state.isButtonPressed(gcpad::Button::Guide))  std::cout << "GUIDE ";
    if (state.isButtonPressed(gcpad::Button::L3))     std::cout << "L3 ";
    if (state.isButtonPressed(gcpad::Button::R3))     std::cout << "R3 ";
    if (state.isButtonPressed(gcpad::Button::DPad_Up))    std::cout << "UP ";
    if (state.isButtonPressed(gcpad::Button::DPad_Down))  std::cout << "DN ";
    if (state.isButtonPressed(gcpad::Button::DPad_Left))  std::cout << "LT ";
    if (state.isButtonPressed(gcpad::Button::DPad_Right)) std::cout << "RT ";

    // Sticks
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "| LStick(" 
              << std::setw(5) << state.getAxis(gcpad::Axis::LeftX) << ","
              << std::setw(5) << state.getAxis(gcpad::Axis::LeftY) << ") ";
    std::cout << "RStick("
              << std::setw(5) << state.getAxis(gcpad::Axis::RightX) << ","
              << std::setw(5) << state.getAxis(gcpad::Axis::RightY) << ") ";

    // Triggers
    std::cout << "LT:" << std::setw(4) << state.getAxis(gcpad::Axis::LeftTrigger)
              << " RT:" << std::setw(4) << state.getAxis(gcpad::Axis::RightTrigger);

    // Battery
    if (state.battery_level > 0.0f) {
        std::cout << " BAT:" << static_cast<int>(state.battery_level * 100) << "%";
        if (state.is_charging) std::cout << "+";
    }

    std::cout << "                    " << std::flush;
}

int main() {
    std::signal(SIGINT, signal_handler);

    std::cout << "=== GCPad API Example ===" << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    std::cout << std::endl;

    // Get build info
    auto meta = gcpad::getBuildMetadata();
    std::cout << "Version: " << meta.package_version 
              << " (commit: " << meta.git_commit << ")" << std::endl;
    std::cout << std::endl;

    // Step 1: List all HID devices (debug)
    std::cout << "--- Scanning HID devices ---" << std::endl;
    auto all_hid = gcpad::getAllHidDevices();
    std::cout << "Found " << all_hid.size() << " HID device(s):" << std::endl;
    for (const auto& dev : all_hid) {
        std::cout << "  VID:PID = 0x" << std::hex << dev.vendor_id 
                  << ":0x" << dev.product_id << std::dec
                  << "  Name: " << (dev.product_string.empty() ? "(unknown)" : dev.product_string)
                  << std::endl;
    }
    std::cout << std::endl;

    // Step 2: Create and initialize the gamepad manager
    // This scans across ALL backends in priority order:
    //   1. Raw HID (PlayStation, Xbox HID, Nintendo)
    //   2. XInput (Xbox controllers without HID)
    //   3. DirectInput (legacy game controllers, flight sticks, wheels)
    //   4. SDL2 GameController/Joystick (broadest compatibility)
    auto manager = gcpad::createGamepadManager();
    
    manager->setGamepadConnectedCallback([](int index) {
        std::cout << "\n[EVENT] Gamepad connected at slot " << index << std::endl;
    });

    manager->setGamepadDisconnectedCallback([](int index) {
        std::cout << "\n[EVENT] Gamepad disconnected from slot " << index << std::endl;
    });

    if (!manager->initialize()) {
        std::cerr << "Failed to initialize GamepadManager: " << manager->getLastError() << std::endl;
        return 1;
    }

    std::cout << "GamepadManager initialized. Max slots: " << manager->getMaxGamepads() << std::endl;
    std::cout << "Connected gamepads: " << manager->getConnectedGamepadCount() << std::endl;

    // Print connected devices
    auto indices = manager->getConnectedGamepadIndices();
    for (int idx : indices) {
        auto* pad = manager->getGamepad(idx);
        if (pad) {
            std::cout << "  Slot " << idx << ": " << pad->getName() << std::endl;
        }
    }
    std::cout << std::endl;

    // Step 3: Set up input remapping (example: right stick -> mouse, A -> Space, B -> Escape)
    gcpad::GamepadInputRemapper remapper;
    remapper.mapButtonToKey(gcpad::Button::A, 0x20);  // VK_SPACE
    remapper.mapButtonToKey(gcpad::Button::B, 0x1B);  // VK_ESCAPE
    remapper.mapButtonToKey(gcpad::Button::X, 'X');    // 'X' key
    remapper.mapButtonToKey(gcpad::Button::Y, 'Y');    // 'Y' key
    remapper.mapButtonToKey(gcpad::Button::Start, VK_RETURN);  // Enter
    remapper.mapButtonToMouseButton(gcpad::Button::R2, gcpad::MouseButton::Left);
    remapper.mapButtonToMouseButton(gcpad::Button::L2, gcpad::MouseButton::Right);
    remapper.mapAxisToMouse(gcpad::Axis::RightX, 15.0f, 0.15f); // Right stick -> mouse X
    remapper.mapAxisToMouse(gcpad::Axis::RightY, 15.0f, 0.15f); // Right stick -> mouse Y

    std::cout << "Input remapping configured:" << std::endl;
    std::cout << "  A -> Space, B -> Escape, X -> 'X', Y -> 'Y'" << std::endl;
    std::cout << "  Start -> Enter" << std::endl;
    std::cout << "  R2 -> Left Click, L2 -> Right Click" << std::endl;
    std::cout << "  Right Stick -> Mouse Movement" << std::endl;
    std::cout << std::endl;
    std::cout << "--- Reading inputs (Ctrl+C to stop) ---" << std::endl;

    gcpad::GamepadState prev_state;

    // Step 4: Main loop - read and remap
    while (running) {
        manager->updateAll();

        // Find first connected gamepad
        auto connected = manager->getConnectedGamepadIndices();
        if (!connected.empty()) {
            auto* pad = manager->getGamepad(connected[0]);
            if (pad && pad->isConnected()) {
                const auto& state = pad->getState();
                print_state(state, connected[0]);

                // Send remapped inputs to Windows
                remapper.sendInput(state, prev_state);
                prev_state = state;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(8)); // ~120Hz poll
    }

    std::cout << std::endl << "Shutting down..." << std::endl;
    manager->shutdown();

    return 0;
}
