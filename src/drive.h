#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dw {

struct DriveInfo {
    int number = -1;      // N in \\.\PhysicalDriveN
    std::string model;    // UTF-8
    std::string serial;   // kann leer sein
    uint64_t size = 0;
    bool usb = false;
    bool removable = false;
    std::vector<std::string> volumes;  // "E:" oder "E: (BEZEICHNUNG)", UTF-8, nach Buchstabe sortiert
    bool system = false;  // enthält das Windows-Volume (oder Systemplatte nicht ermittelbar)
};

// Systemplatte und Laufwerke ohne Kapazität nie; sonst USB/Wechselmedium oder intern mit Opt-in.
bool isSelectable(const DriveInfo& d, bool includeInternal);

std::string formatBytes(uint64_t bytes);
std::string describeDrive(const DriveInfo& d);
// Beschreibungen in gleicher Reihenfolge; Duplikate (Modell+Größe+Typ) mit verschiedenen Seriennummern erhalten " [SN …XXXX]".
std::vector<std::string> describeDrives(const std::vector<DriveInfo>& drives);

}  // namespace dw
