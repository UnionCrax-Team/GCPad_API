#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include <Windows.h>

namespace gcpad {
namespace internal {

struct HidDeviceAttributes {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version_number;
};

struct HidDeviceCapabilities {
    uint16_t usage;
    uint16_t usage_page;
    uint16_t input_report_byte_length;
    uint16_t output_report_byte_length;
    uint16_t feature_report_byte_length;
};

class HidDevice {
public:
    explicit HidDevice(const std::string& device_path);
    ~HidDevice();

    bool open();
    void close();
    bool is_open() const;

    HidDeviceAttributes get_attributes() const;
    HidDeviceCapabilities get_capabilities() const;
    std::string get_product_string() const;
    std::string get_manufacturer_string() const;
    std::string get_serial_number_string() const;

    bool read(std::vector<uint8_t>& buffer);
    bool write(const std::vector<uint8_t>& buffer);
    bool read_feature(std::vector<uint8_t>& buffer);
    bool write_feature(const std::vector<uint8_t>& buffer);

    std::string device_path() const { return device_path_; }

private:
    std::string device_path_;
    HANDLE handle_;
    OVERLAPPED read_overlapped_;
    OVERLAPPED write_overlapped_;
};

std::vector<std::string> enumerate_hid_device_paths(GUID guid, uint16_t vendor_id = 0, uint16_t product_id = 0);
std::vector<std::unique_ptr<HidDevice>> enumerate_hid_devices(uint16_t vendor_id = 0, uint16_t product_id = 0);

} // namespace internal
} // namespace gcpad
