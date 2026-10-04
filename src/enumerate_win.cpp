#include "enumerate_win.h"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstring>
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
    std::vector<DriveInfo> drives;

    for (int n = 0; n < 64; ++n) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
        HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        DriveInfo d = queryDrive(h, n);
        CloseHandle(h);

        const bool onSystemVolume = std::find(system.begin(), system.end(), static_cast<DWORD>(n)) != system.end();
        d.system = systemKnown ? onSystemVolume : !(d.usb || d.removable);
        drives.push_back(d);
    }
    return drives;
}

}  // namespace dw
