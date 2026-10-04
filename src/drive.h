#pragma once
#include <cstdint>
#include <string>

namespace dw {

struct DriveInfo {
    int number = -1;      // N in \\.\PhysicalDriveN
    std::string model;    // UTF-8
    uint64_t size = 0;
    bool usb = false;
    bool removable = false;
    bool system = false;  // enthält das Windows-Volume (oder Systemplatte nicht ermittelbar)
};

// Systemplatte und Laufwerke ohne Kapazität nie; sonst USB/Wechselmedium oder intern mit Opt-in.
bool isSelectable(const DriveInfo& d, bool includeInternal);

std::string formatBytes(uint64_t bytes);
std::string describeDrive(const DriveInfo& d);

}  // namespace dw
