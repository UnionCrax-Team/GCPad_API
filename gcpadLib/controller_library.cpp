#include "controller_library.h"
#include "ps_device.h"
#include "xbox_device.h"
#include "nintendo_device.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace gcpad {

static std::vector<ControllerLibraryEntry> buildPlayStationLibrary() {
    std::vector<ControllerLibraryEntry> list;
    for (auto& info : internal::getPlayStationDeviceInfos()) {
        list.push_back({ControllerBrand::PlayStation, info.vendor_id, info.product_id, info.name, "HID usage page 0x01 / usage 0x05 (Gamepad)"});
    }
    return list;
}

static std::vector<ControllerLibraryEntry> buildXboxLibrary() {
    std::vector<ControllerLibraryEntry> list;
    for (auto& info : internal::getXboxDeviceInfos()) {
        list.push_back({ControllerBrand::Xbox, info.vendor_id, info.product_id, info.name, "XInput / Xbox HID specification"});
    }
    return list;
}

static std::vector<ControllerLibraryEntry> buildNintendoLibrary() {
    std::vector<ControllerLibraryEntry> list;
    for (auto& info : internal::getNintendoDeviceInfos()) {
        list.push_back({ControllerBrand::Nintendo, info.vendor_id, info.product_id, info.name, "Nintendo Joy-Con/Pro profile"});
    }
    return list;
}

std::vector<ControllerLibraryEntry> getPlayStationControllerLibrary() {
    return buildPlayStationLibrary();
}

std::vector<ControllerLibraryEntry> getXboxControllerLibrary() {
    return buildXboxLibrary();
}

std::vector<ControllerLibraryEntry> getNintendoControllerLibrary() {
    return buildNintendoLibrary();
}

std::vector<ControllerLibraryEntry> getAllKnownControllers() {
    auto master = buildPlayStationLibrary();
    auto xbox = buildXboxLibrary();
    auto nintendo = buildNintendoLibrary();
    master.reserve(master.size() + xbox.size() + nintendo.size());
    master.insert(master.end(), xbox.begin(), xbox.end());
    master.insert(master.end(), nintendo.begin(), nintendo.end());
    return master;
}

bool writeControllerLibraryFiles(const std::string& directory) {
    fs::path dir = fs::u8path(directory);
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        if (!fs::create_directories(dir, ec)) {
            std::cerr << "[DEBUG] Failed to create directory " << dir.u8string() << " (" << ec.value() << "): " << ec.message() << "\n";
            return false;
        }
    }

    auto write = [&](const std::string& filename, const std::vector<ControllerLibraryEntry>& entries) {
        auto file_path = (dir / filename).u8string();
        std::ofstream out(file_path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "[DEBUG] Failed to open file " << file_path << " for writing\n";
            return false;
        }
        out << "brand,vendor_id,product_id,name,interface_hint\n";
        for (auto& e : entries) {
            out << static_cast<int>(e.brand) << ",0x" << std::hex << e.vendor_id << ",0x" << std::hex << e.product_id
                << ",\"" << e.name << "\",\"" << e.interface_hint << "\"\n";
            out << std::dec;
        }
        return true;
    };

    bool ok = true;
    ok &= write("ps_library.csv", getPlayStationControllerLibrary());
    ok &= write("xbox_library.csv", getXboxControllerLibrary());
    ok &= write("nintendo_library.csv", getNintendoControllerLibrary());
    ok &= write("all_controllers.csv", getAllKnownControllers());
    return ok;
}

} // namespace gcpad
