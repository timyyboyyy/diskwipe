#include "drive.h"

#include <cstdio>

#include "pattern.h"

namespace dw {

bool isSelectable(const DriveInfo& d, bool includeInternal) {
    if (d.system || d.size < kBlockSize) return false;
    return d.usb || d.removable || includeInternal;
}

std::string formatBytes(uint64_t bytes) {
    static const char* const units[] = {"B", "KB", "MB", "GB", "TB"};
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double v = static_cast<double>(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        ++i;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[i]);
    std::string s(buf);
    const size_t dot = s.find('.');
    if (dot != std::string::npos) s[dot] = ',';
    return s;
}

namespace {

const char* driveKind(const DriveInfo& d) { return d.usb ? "USB" : d.removable ? "Wechseldatenträger" : "Intern"; }

std::string modelOrUnknown(const DriveInfo& d) { return d.model.empty() ? "Unbekannt" : d.model; }

// Modell + Größe + Typ: gleiche Basis bedeutet "nicht unterscheidbar".
std::string baseKey(const DriveInfo& d) { return modelOrUnknown(d) + "|" + formatBytes(d.size) + "|" + driveKind(d); }

std::string build(const DriveInfo& d, const std::string& modelSuffix) {
    std::string s = "Disk " + std::to_string(d.number) + " – " + modelOrUnknown(d) + modelSuffix + " – " +
                    formatBytes(d.size) + " – " + driveKind(d);
    for (size_t i = 0; i < d.volumes.size(); ++i) s += (i == 0 ? " – " : ", ") + d.volumes[i];
    return s;
}

}  // namespace

std::string describeDrive(const DriveInfo& d) { return build(d, ""); }

std::vector<std::string> describeDrives(const std::vector<DriveInfo>& drives) {
    std::vector<std::string> out;
    for (const DriveInfo& d : drives) {
        std::string suffix;
        if (!d.serial.empty()) {
            const std::string key = baseKey(d);
            bool differs = false;
            for (const DriveInfo& o : drives)
                if (&o != &d && baseKey(o) == key && o.serial != d.serial) differs = true;
            if (differs) suffix = " [SN …" + (d.serial.size() > 4 ? d.serial.substr(d.serial.size() - 4) : d.serial) + "]";
        }
        out.push_back(build(d, suffix));
    }
    return out;
}

}  // namespace dw
