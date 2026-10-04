#include "enumerate_win.h"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>

#include "util_win.h"

namespace dw {
namespace {

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::string descriptorString(const std::vector<BYTE>& buf, DWORD returned, DWORD offset) {
    if (offset == 0 || offset >= returned) return "";
    const char* p = reinterpret_cast<const char*>(buf.data() + offset);
    return trim(std::string(p, strnlen(p, returned - offset)));
}

bool systemDiskNumbers(std::vector<DWORD>& out) {
    wchar_t dir[MAX_PATH];
    const UINT n = GetSystemWindowsDirectoryW(dir, MAX_PATH);
    if (n < 2 || n >= MAX_PATH || dir[1] != L':') return false;
    std::wstring path = L"\\\\.\\";
    path += dir[0];
    path += L':';
    HANDLE v = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (v == INVALID_HANDLE_VALUE) return false;
    out = volumeDiskNumbers(v);
    CloseHandle(v);
    if (out.empty()) return false;

    // Firmware-Systempartition (ESP) kann auf einem anderen Laufwerk liegen. Fehler hier ändern nichts am Ergebnis.
    wchar_t part[MAX_PATH];
    DWORD partSize = sizeof(part);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"SystemPartition", RRF_RT_REG_SZ, nullptr, part, &partSize) ==
        ERROR_SUCCESS) {
        const std::wstring espPath = std::wstring(L"\\\\?\\GLOBALROOT") + part;
        HANDLE e = CreateFileW(espPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (e != INVALID_HANDLE_VALUE) {
            for (DWORD n : volumeDiskNumbers(e))
                if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
            CloseHandle(e);
        }
    }
    return true;
}

struct VolumeEntry {
    DWORD disk;
    std::string text;  // "E:" oder "E: (LABEL)"
};

// Alle Volumes mit Laufwerksbuchstaben, die genau auf einem physischen Laufwerk liegen. Öffnet nur mit Zugriff 0.
std::vector<VolumeEntry> volumeEntries() {
    std::vector<VolumeEntry> out;
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring path = name;
        if (path.size() < 2 || path.back() != L'\\') continue;
        const std::wstring volumeName = path;  // mit abschließendem Backslash
        path.pop_back();
        HANDLE v = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (v == INVALID_HANDLE_VALUE) continue;
        const std::vector<DWORD> disks = volumeDiskNumbers(v);
        CloseHandle(v);
        if (disks.size() != 1) continue;

        wchar_t label[MAX_PATH + 1] = {};
        if (!GetVolumeInformationW(volumeName.c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0))
            label[0] = 0;

        wchar_t names[512] = {};
        DWORD len = 0;
        if (!GetVolumePathNamesForVolumeNameW(volumeName.c_str(), names, 512, &len)) continue;
        for (const wchar_t* p = names; *p; p += wcslen(p) + 1) {
            if (!(p[0] >= L'A' && p[0] <= L'Z') || p[1] != L':') continue;
            std::string text = toUtf8(std::wstring(p, 2));
            if (label[0]) text += " (" + toUtf8(label) + ")";
            out.push_back({disks[0], text});
        }
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
    std::sort(out.begin(), out.end(), [](const VolumeEntry& a, const VolumeEntry& b) { return a.text < b.text; });
    return out;
}

}  // namespace

DriveInfo queryDrive(HANDLE h, int number) {
    DriveInfo d;
    d.number = number;

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    std::vector<BYTE> buf(1024);
    DWORD returned = 0;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buf.data(), static_cast<DWORD>(buf.size()),
                        &returned, nullptr) &&
        returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buf.data());
        d.usb = desc->BusType == BusTypeUsb;
        d.removable = desc->RemovableMedia != FALSE;
        d.model = trim(descriptorString(buf, returned, desc->VendorIdOffset) + " " +
                       descriptorString(buf, returned, desc->ProductIdOffset));
        d.serial = descriptorString(buf, returned, desc->SerialNumberOffset);
    }

    std::vector<BYTE> geo(256);
    DWORD geoReturned = 0;
    if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, geo.data(), static_cast<DWORD>(geo.size()),
                        &geoReturned, nullptr))
        d.size = static_cast<uint64_t>(reinterpret_cast<const DISK_GEOMETRY_EX*>(geo.data())->DiskSize.QuadPart);
    return d;
}

std::vector<DriveInfo> listDrives() {
    std::vector<DWORD> system;
    const bool systemKnown = systemDiskNumbers(system);
    const std::vector<VolumeEntry> volumes = volumeEntries();
    std::vector<DriveInfo> drives;

    for (int n = 0; n < 64; ++n) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
        HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        DriveInfo d = queryDrive(h, n);
        CloseHandle(h);

        const bool onSystemVolume = std::find(system.begin(), system.end(), static_cast<DWORD>(n)) != system.end();
        d.system = systemKnown ? onSystemVolume : !(d.usb || d.removable);
        for (const VolumeEntry& v : volumes)
            if (v.disk == static_cast<DWORD>(n)) d.volumes.push_back(v.text);
        drives.push_back(d);
    }
    return drives;
}

}  // namespace dw
