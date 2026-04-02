#include "usb_interface_parser.h"
#include "ps_device.h"
#include "xbox_device.h"
#include "nintendo_device.h"

namespace gcpad {

static ControllerBrand identifyController(uint16_t vid, uint16_t pid) {
    for (auto& info : internal::getPlayStationDeviceInfos()) {
        if (info.vendor_id == vid && info.product_id == pid) return ControllerBrand::PlayStation;
    }
    for (auto& info : internal::getXboxDeviceInfos()) {
        if (info.vendor_id == vid && info.product_id == pid) return ControllerBrand::Xbox;
    }
    for (auto& info : internal::getNintendoDeviceInfos()) {
        if (info.vendor_id == vid && info.product_id == pid) return ControllerBrand::Nintendo;
    }
    return ControllerBrand::Unknown;
}

static UsbInterfaceInterpretation interpretUsbInterfaceInternal(internal::HidDevice& device) {
    UsbInterfaceInterpretation result;

    if (!device.is_open()) {
        result.advice = "Device not open; try open() first or run as Administrator.";
        return result;
    }

    auto attrs = device.get_attributes();
    auto caps = device.get_capabilities();
    result.vendor_id = attrs.vendor_id;
    result.product_id = attrs.product_id;
    result.usage = caps.usage;
    result.usage_page = caps.usage_page;
    result.manufacturer = device.get_manufacturer_string();
    result.product = device.get_product_string();
    result.serial_number = device.get_serial_number_string();
    result.connection_type = ConnectionType::USB;
    result.controller_brand = identifyController(attrs.vendor_id, attrs.product_id);

    if (result.controller_brand == ControllerBrand::Unknown) {
        if ((caps.usage_page == 0x01 && caps.usage == 0x05) || (caps.usage_page == 0x01 && caps.usage == 0x04)) {
            result.advice = "Generic Gamepad/HID. Can be mapped as GHID API or xinput bridge.";
        } else {
            result.advice = "Unknown HID usage page. Add to controller library if this is a new controller.";
        }
    } else {
        result.advice = "Known controller: " + std::string((result.controller_brand == ControllerBrand::PlayStation) ? "PlayStation" : (result.controller_brand == ControllerBrand::Xbox) ? "Xbox" : "Nintendo");
        if (result.controller_brand == ControllerBrand::PlayStation && caps.usage_page == 0x01) {
            result.connection_type = ConnectionType::USB;
        }
    }

    if (result.product.empty()) {
        result.advice += " Product string unavailable; may require elevated permissions.";
    }

    return result;
}

UsbInterfaceInterpretation interpretUsbInterface(const std::string& device_path) {
    internal::HidDevice device(device_path);
    if (!device.open()) {
        UsbInterfaceInterpretation result;
        result.advice = "Unable to open HID device. Run as Administrator or driver must be unlocked.";
        return result;
    }

    auto result = interpretUsbInterfaceInternal(device);
    device.close();
    return result;
}

} // namespace gcpad
