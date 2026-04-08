#pragma once

#include <string>
#include <cstdint>

#include "gcpad.h"
#include "hid_device.h"
#include "controller_library.h"

namespace gcpad {

enum class ConnectionType {
    Unknown,
    USB,
    Bluetooth,
};

struct GCPAD_API UsbInterfaceInterpretation {
    ControllerBrand controller_brand = ControllerBrand::Unknown;
    ConnectionType connection_type = ConnectionType::Unknown;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    uint16_t usage = 0;
    uint16_t usage_page = 0;
    std::string manufacturer;
    std::string product;
    std::string serial_number;
    std::string advice;
};

GCPAD_API UsbInterfaceInterpretation interpretUsbInterface(const std::string& device_path);

} // namespace gcpad
