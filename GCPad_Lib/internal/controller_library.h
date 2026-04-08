#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "gcpad.h"

namespace gcpad {

enum class ControllerBrand {
    Unknown,
    PlayStation,
    Xbox,
    Nintendo,
};

struct GCPAD_API ControllerLibraryEntry {
    ControllerBrand brand;
    uint16_t vendor_id;
    uint16_t product_id;
    std::string name;
    std::string interface_hint;
};

GCPAD_API std::vector<ControllerLibraryEntry> getPlayStationControllerLibrary();
GCPAD_API std::vector<ControllerLibraryEntry> getXboxControllerLibrary();
GCPAD_API std::vector<ControllerLibraryEntry> getNintendoControllerLibrary();
GCPAD_API std::vector<ControllerLibraryEntry> getAllKnownControllers();

// Emits JSON/text library files under directory for easy inspection.
GCPAD_API bool writeControllerLibraryFiles(const std::string& directory);

} // namespace gcpad
