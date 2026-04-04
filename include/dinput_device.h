#pragma once

#include "gcpad.h"
#include "GamepadManager.h"
#include <memory>
#include <vector>
#include <string>

namespace gcpad {
namespace internal {

/// Information about a DirectInput device discovered during enumeration.
struct DInputDeviceInfo {
    std::string instance_guid;  // Stringified GUID used as stable identifier
    std::string product_name;
    uint16_t vendor_id;
    uint16_t product_id;
};

/// Create a GamepadDevice backed by DirectInput for the given instance GUID.
GCPAD_API std::unique_ptr<GamepadDevice> createDInputDevice(
    const std::string& instance_guid, int slot);

/// Enumerate all DirectInput game controllers that are NOT XInput devices.
/// This avoids double-detection since XInput devices also appear in DirectInput.
GCPAD_API std::vector<DInputDeviceInfo> enumerateDInputDevices();

/// Initialize the DirectInput COM subsystem (call once at startup).
GCPAD_API bool initializeDInput();

/// Shutdown the DirectInput COM subsystem.
GCPAD_API void shutdownDInput();

} // namespace internal
} // namespace gcpad
