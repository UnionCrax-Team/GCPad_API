#pragma once

#include "gcpad.h"
#include "GamepadManager.h"
#include <memory>
#include <vector>

namespace gcpad {
namespace internal {

// XInput device - for Xbox controllers that don't expose via raw HID
// (which is most of them on Windows without special drivers)
GCPAD_API std::unique_ptr<GamepadDevice> createXInputDevice(int xinput_index, int slot);

// Get a list of connected XInput device indices (0-3)
GCPAD_API std::vector<int> getConnectedXInputIndices();

} // namespace internal
} // namespace gcpad
