#pragma once

#include "gcpad.h"
#include "GamepadManager.h"
#include "hid_device.h"
#include <memory>
#include <vector>

namespace gcpad {
namespace internal {

struct XboxDeviceInfo {
    uint16_t vendor_id;
    uint16_t product_id;
    std::string name;
};

GCPAD_API std::vector<XboxDeviceInfo> getXboxDeviceInfos();
GCPAD_API std::unique_ptr<GamepadDevice> createXboxDevice(std::unique_ptr<HidDevice> hid_device, int index);

} // namespace internal
} // namespace gcpad