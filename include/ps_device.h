#pragma once

#include "gcpad.h"
#include "GamepadManager.h"
#include "hid_device.h"
#include <memory>
#include <vector>

// The internal PS device API (not part of public GamepadManager interface)
namespace gcpad {
namespace internal {

struct PlayStationDeviceInfo {
    uint16_t vendor_id;
    uint16_t product_id;
    std::string name;
};

// Helper factory and pids
GCPAD_API std::vector<PlayStationDeviceInfo> getPlayStationDeviceInfos();
GCPAD_API std::unique_ptr<GamepadDevice> createPlayStationDevice(std::unique_ptr<HidDevice> hid_device, int index);

} // namespace internal
} // namespace gcpad