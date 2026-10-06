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

const char* driveKind(const DriveInfo& d);  // "USB", "Wechseldatenträger" oder "Intern"

// Derselbe physische Datenträger (Nummer egal): Modell, Seriennummer, Größe, USB, Removable gleich.
// Ohne Seriennummer ist die Identität nie gesichert.
bool sameIdentity(const DriveInfo& a, const DriveInfo& b);
// Indizes aller Nicht-System-Laufwerke mit derselben Identität wie id.
std::vector<size_t> findByIdentity(const std::vector<DriveInfo>& list, const DriveInfo& id);

}  // namespace dw
