#include "hid_device.h"
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace gcpad {
namespace internal {

// Helper function to get string descriptor
static std::string get_string_descriptor(HANDLE handle, uint8_t index) {
    if (index == 0) return "";

    std::vector<wchar_t> buffer(256);
    if (HidD_GetIndexedString(handle, index, reinterpret_cast<PVOID>(buffer.data()), static_cast<ULONG>(buffer.size() * sizeof(wchar_t)))) {
        std::string result;
        for (auto& c : buffer) {
            if (c == L'\0') break;
            result += static_cast<char>(c);
        }
        return result;
    }
    return "";
}

HidDevice::HidDevice(const std::string& device_path)
    : device_path_(device_path), handle_(INVALID_HANDLE_VALUE) {
    memset(&read_overlapped_, 0, sizeof(read_overlapped_));
    memset(&write_overlapped_, 0, sizeof(write_overlapped_));
}

bool HidDevice::is_open() const {
    return handle_ != INVALID_HANDLE_VALUE;
}

HidDevice::~HidDevice() {
    close();
}

bool HidDevice::open() {
    if (is_open()) return true;

    handle_ = CreateFileA(device_path_.c_str(),
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr,
                         OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED,
                         nullptr);

    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Create overlapped events
    read_overlapped_.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    write_overlapped_.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    return true;
}

void HidDevice::close() {
    if (!is_open()) return;

    CancelIo(handle_);
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;

    if (read_overlapped_.hEvent) {
        CloseHandle(read_overlapped_.hEvent);
        read_overlapped_.hEvent = nullptr;
    }

    if (write_overlapped_.hEvent) {
        CloseHandle(write_overlapped_.hEvent);
        write_overlapped_.hEvent = nullptr;
    }
}

HidDeviceAttributes HidDevice::get_attributes() const {
    HIDD_ATTRIBUTES attributes = { sizeof(HIDD_ATTRIBUTES) };
    HidD_GetAttributes(handle_, &attributes);

    return {
        attributes.VendorID,
        attributes.ProductID,
        attributes.VersionNumber
    };
}

HidDeviceCapabilities HidDevice::get_capabilities() const {
    HIDP_CAPS caps;
    PHIDP_PREPARSED_DATA preparsed_data = nullptr;

    if (!HidD_GetPreparsedData(handle_, &preparsed_data)) {
        return {};
    }

    HidP_GetCaps(preparsed_data, &caps);
    HidD_FreePreparsedData(preparsed_data);

    return {
        caps.Usage,
        caps.UsagePage,
        caps.InputReportByteLength,
        caps.OutputReportByteLength,
        caps.FeatureReportByteLength
    };
}

std::string HidDevice::get_product_string() const {
    if (!is_open()) return "";
    return get_string_descriptor(handle_, HidD_GetProductString(handle_, nullptr, 0) ? 1 : 0);
}

std::string HidDevice::get_manufacturer_string() const {
    if (!is_open()) return "";
    return get_string_descriptor(handle_, HidD_GetManufacturerString(handle_, nullptr, 0) ? 1 : 0);
}

std::string HidDevice::get_serial_number_string() const {
    if (!is_open()) return "";
    return get_string_descriptor(handle_, HidD_GetSerialNumberString(handle_, nullptr, 0) ? 1 : 0);
}

bool HidDevice::read(std::vector<uint8_t>& buffer) {
    if (!is_open()) return false;

    auto caps = get_capabilities();
    buffer.resize(caps.input_report_byte_length);

    DWORD bytes_read;
    if (!ReadFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, &read_overlapped_)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return false;
        }

        if (!GetOverlappedResult(handle_, &read_overlapped_, &bytes_read, TRUE)) {
            return false;
        }
    }

    return true;
}

bool HidDevice::write(const std::vector<uint8_t>& buffer) {
    if (!is_open()) return false;

    DWORD bytes_written;
    if (!WriteFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_written, &write_overlapped_)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return false;
        }

        if (!GetOverlappedResult(handle_, &write_overlapped_, &bytes_written, TRUE)) {
            return false;
        }
    }

    return bytes_written == buffer.size();
}

bool HidDevice::read_feature(std::vector<uint8_t>& buffer) {
    if (!is_open()) return false;

    auto caps = get_capabilities();
    buffer.resize(caps.feature_report_byte_length);

    return HidD_GetFeature(handle_, buffer.data(), static_cast<ULONG>(buffer.size()));
}

bool HidDevice::write_feature(const std::vector<uint8_t>& buffer) {
    if (!is_open()) return false;

    return HidD_SetFeature(handle_, const_cast<uint8_t*>(buffer.data()), static_cast<ULONG>(buffer.size()));
}

// HID Device enumeration
std::vector<std::string> enumerate_hid_device_paths(GUID guid, uint16_t vendor_id, uint16_t product_id) {
    std::vector<std::string> device_paths;

    HDEVINFO device_info = SetupDiGetClassDevsA(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) {
        return device_paths;
    }

    SP_DEVICE_INTERFACE_DATA device_interface_data = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    DWORD index = 0;

    while (SetupDiEnumDeviceInterfaces(device_info, nullptr, &guid, index++, &device_interface_data)) {
        DWORD required_size = 0;
        SetupDiGetDeviceInterfaceDetailA(device_info, &device_interface_data, nullptr, 0, &required_size, nullptr);

        std::vector<uint8_t> buffer(required_size);
        auto* detail_data = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_A>(buffer.data());
        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        SP_DEVINFO_DATA device_info_data = { sizeof(SP_DEVINFO_DATA) };

        if (SetupDiGetDeviceInterfaceDetailA(device_info, &device_interface_data, detail_data, required_size, nullptr, &device_info_data)) {
            std::string device_path = detail_data->DevicePath;

            // If vendor_id and product_id are specified, filter by them
            if (vendor_id != 0 || product_id != 0) {
                // Open device temporarily to check VID/PID
                HANDLE temp_handle = CreateFileA(device_path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (temp_handle != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES attributes = { sizeof(HIDD_ATTRIBUTES) };
                    if (HidD_GetAttributes(temp_handle, &attributes)) {
                        if ((vendor_id == 0 || attributes.VendorID == vendor_id) &&
                            (product_id == 0 || attributes.ProductID == product_id)) {
                            device_paths.push_back(device_path);
                        }
                    }
                    CloseHandle(temp_handle);
                }
            } else {
                device_paths.push_back(device_path);
            }
        }
    }

    SetupDiDestroyDeviceInfoList(device_info);
    return device_paths;
}

std::vector<std::unique_ptr<HidDevice>> enumerate_hid_devices(uint16_t vendor_id, uint16_t product_id) {
    std::vector<std::unique_ptr<HidDevice>> devices;

    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    auto device_paths = enumerate_hid_device_paths(hid_guid, vendor_id, product_id);

    for (const auto& path : device_paths) {
        devices.push_back(std::make_unique<HidDevice>(path));
    }

    return devices;
}

} // namespace internal
} // namespace gcpad