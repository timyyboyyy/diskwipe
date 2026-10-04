#include "device_win.h"

#include <winioctl.h>

#include <algorithm>

#include "enumerate_win.h"
#include "util_win.h"

namespace dw {

std::unique_ptr<WinPhysicalDevice> WinPhysicalDevice::open(const DriveInfo& expected, std::string& err) {
    const int diskNumber = expected.number;

    // 0. Laufwerk gegenüber der Auswahl revalidieren (Nummern ändern sich beim Umstecken).
    bool unchanged = false;
    for (const DriveInfo& cur : listDrives()) {
        if (cur.number != expected.number) continue;
        unchanged = !cur.system && cur.model == expected.model && cur.serial == expected.serial &&
                    cur.size == expected.size && cur.usb == expected.usb &&
                    cur.removable == expected.removable;
    }
    if (!unchanged) {
        err = "Laufwerk hat sich geändert – bitte Liste aktualisieren";
        return nullptr;
    }

    std::unique_ptr<WinPhysicalDevice> d(new WinPhysicalDevice());
    DWORD ret = 0;

    // 1. Alle Volumes auf diesem Laufwerk sperren und aushängen.
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring path(name);
            if (!path.empty() && path.back() == L'\\') path.pop_back();
            // Erst nur abfragen (Zugriffsrecht 0): liegt das Volume auf dem Ziellaufwerk?
            HANDLE probe = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (probe == INVALID_HANDLE_VALUE) continue;  // kein Datenträger-Volume
            const auto disks = volumeDiskNumbers(probe);
            CloseHandle(probe);
            if (std::find(disks.begin(), disks.end(), static_cast<DWORD>(diskNumber)) == disks.end()) continue;

            HANDLE v = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (v == INVALID_HANDLE_VALUE) {
                err = "Volume auf dem Laufwerk kann nicht geöffnet werden: " + winErrorText(GetLastError());
                FindVolumeClose(find);
                return nullptr;
            }
            bool locked = false;
            DWORD lockError = 0;
            for (int attempt = 0; attempt < 10 && !locked; ++attempt) {
                locked = DeviceIoControl(v, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr) != FALSE;
                if (!locked) {
                    lockError = GetLastError();
                    Sleep(200);
                }
            }
            if (!locked) {
                err = "Laufwerk in Benutzung – Explorer-Fenster/Programme schließen (" + winErrorText(lockError) + ")";
                CloseHandle(v);
                FindVolumeClose(find);
                return nullptr;
            }
            d->volumes_.push_back(v);
            if (!DeviceIoControl(v, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr)) {
                err = "Volume konnte nicht ausgehängt werden: " + winErrorText(GetLastError());
                FindVolumeClose(find);
                return nullptr;
            }
        } while (FindNextVolumeW(find, name, MAX_PATH));
        FindVolumeClose(find);
    }

    // 2. Laufwerk roh öffnen, am Cache vorbei.
    const std::wstring diskPath = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskNumber);
    d->disk_ = CreateFileW(diskPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (d->disk_ == INVALID_HANDLE_VALUE) {
        err = "Laufwerk konnte nicht geöffnet werden: " + winErrorText(GetLastError());
        return nullptr;
    }

    // 2b. Identität am geöffneten Handle erneut prüfen (schließt das Zeitfenster seit Schritt 0).
    {
        const DriveInfo opened = queryDrive(d->disk_, diskNumber);
        if (opened.model != expected.model || opened.serial != expected.serial || opened.size != expected.size ||
            opened.usb != expected.usb || opened.removable != expected.removable) {
            err = "Laufwerk hat sich geändert – bitte Liste aktualisieren";
            return nullptr;
        }
    }

    // 3. Größe und Sektorgröße.
    GET_LENGTH_INFORMATION length{};
    if (!DeviceIoControl(d->disk_, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length, sizeof(length), &ret, nullptr)) {
        err = "Größe nicht ermittelbar: " + winErrorText(GetLastError());
        return nullptr;
    }
    d->size_ = static_cast<uint64_t>(length.Length.QuadPart);
    std::vector<BYTE> geo(256);
    if (DeviceIoControl(d->disk_, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, geo.data(), static_cast<DWORD>(geo.size()),
                        &ret, nullptr)) {
        const DWORD bps = reinterpret_cast<const DISK_GEOMETRY_EX*>(geo.data())->Geometry.BytesPerSector;
        if (bps != 0) d->sector_ = bps;
    }
    return d;
}

WinPhysicalDevice::~WinPhysicalDevice() {
    DWORD ret = 0;
    if (disk_ != INVALID_HANDLE_VALUE) {
        DeviceIoControl(disk_, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &ret, nullptr);
        CloseHandle(disk_);
    }
    for (HANDLE v : volumes_) {
        DeviceIoControl(v, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr);
        CloseHandle(v);
    }
}

bool WinPhysicalDevice::read(uint64_t offset, uint8_t* buf, size_t len) {
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD got = 0;
    if (!ReadFile(disk_, buf, static_cast<DWORD>(len), &got, &ov)) {
        err_ = winErrorText(GetLastError());
        return false;
    }
    if (got != len) {
        err_ = "unvollständig gelesen (" + std::to_string(got) + " von " + std::to_string(len) + " Bytes)";
        return false;
    }
    return true;
}

bool WinPhysicalDevice::write(uint64_t offset, const uint8_t* buf, size_t len) {
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD written = 0;
    if (!WriteFile(disk_, buf, static_cast<DWORD>(len), &written, &ov)) {
        err_ = winErrorText(GetLastError());
        return false;
    }
    if (written != len) {
        err_ = "unvollständig geschrieben (" + std::to_string(written) + " von " + std::to_string(len) + " Bytes)";
        return false;
    }
    return true;
}

bool WinPhysicalDevice::flush() {
    if (!FlushFileBuffers(disk_)) {
        err_ = winErrorText(GetLastError());
        return false;
    }
    return true;
}

}  // namespace dw
