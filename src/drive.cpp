#include "drive.h"

#include <cstdio>

namespace dw {

bool isSelectable(const DriveInfo& d, bool includeInternal) {
    if (d.system || d.size == 0) return false;
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

std::string describeDrive(const DriveInfo& d) {
    const char* kind = d.usb ? "USB" : d.removable ? "Wechseldatenträger" : "Intern";
    return "Disk " + std::to_string(d.number) + " – " + (d.model.empty() ? "Unbekannt" : d.model) + " – " +
           formatBytes(d.size) + " – " + kind;
}

}  // namespace dw
