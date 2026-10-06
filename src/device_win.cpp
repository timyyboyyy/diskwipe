#include "device_win.h"

#include <winioctl.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>

#include "audit_log.h"
#include "enumerate_win.h"
#include "util_win.h"

namespace dw {
namespace {

using LogFn = std::function<void(const std::string&)>;

std::string guidText(const GUID& g) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", static_cast<unsigned long>(g.Data1),
                  g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
                  g.Data4[7]);
    return buf;
}

// Partitionstabelle vor dem Löschen protokollieren (Abfrage ohne Lese-/Schreibzugriff).
void logPartitionTable(int diskNumber, const LogFn& log) {
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskNumber);
    HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log("Partitionstabelle nicht lesbar: " + winErrorText(GetLastError()));
        return;
    }
    std::vector<BYTE> buf(sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 128 * sizeof(PARTITION_INFORMATION_EX));
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, buf.data(), static_cast<DWORD>(buf.size()), &ret,
                         nullptr)) {
        log("Partitionstabelle nicht lesbar: " + winErrorText(GetLastError()));
        CloseHandle(h);
        return;
    }
    CloseHandle(h);
    const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(buf.data());
    const char* style = layout->PartitionStyle == PARTITION_STYLE_MBR   ? "MBR"
                        : layout->PartitionStyle == PARTITION_STYLE_GPT ? "GPT"
                                                                        : "keine (RAW)";
    std::vector<const PARTITION_INFORMATION_EX*> parts;
    for (DWORD i = 0; i < layout->PartitionCount; ++i)
        if (layout->PartitionEntry[i].PartitionLength.QuadPart > 0) parts.push_back(&layout->PartitionEntry[i]);
    log(std::string("Partitionstabelle vor dem Löschen: ") + style + ", " + std::to_string(parts.size()) + " Partition(en)");
    for (size_t i = 0; i < parts.size(); ++i) {
        const PARTITION_INFORMATION_EX& p = *parts[i];
        std::string text = "  Partition " + std::to_string(i + 1) + ": Offset " +
                           groupDigits(static_cast<uint64_t>(p.StartingOffset.QuadPart)) + ", Größe " +
                           groupDigits(static_cast<uint64_t>(p.PartitionLength.QuadPart)) + " Bytes";
        if (p.PartitionStyle == PARTITION_STYLE_MBR) {
            char type[16];
            std::snprintf(type, sizeof(type), "0x%02X", p.Mbr.PartitionType);
            text += std::string(", Typ ") + type + (p.Mbr.BootIndicator ? ", aktiv" : "");
        } else if (p.PartitionStyle == PARTITION_STYLE_GPT) {
            text += ", Typ " + guidText(p.Gpt.PartitionType);
            const std::wstring name(p.Gpt.Name, wcsnlen(p.Gpt.Name, 36));
            if (!name.empty()) text += ", Name \"" + toUtf8(name) + "\"";
        }
        log(text);
    }
}

// Laufwerksbuchstaben/Bereitstellungspunkte eines Volumes ("E:\"), leer wenn keine.
std::string mountPoints(const wchar_t* volumeName) {
    wchar_t buf[512] = {};
    DWORD len = 0;
    if (!GetVolumePathNamesForVolumeNameW(volumeName, buf, 512, &len)) return {};
    std::string out;
    for (const wchar_t* p = buf; *p; p += wcslen(p) + 1) out += (out.empty() ? "" : ", ") + toUtf8(p);
    return out;
}

}  // namespace

std::unique_ptr<WinPhysicalDevice> WinPhysicalDevice::open(const DriveInfo& expected, std::string& err, const LogFn& logFn) {
    const int diskNumber = expected.number;
    const LogFn log = logFn ? logFn : [](const std::string&) {};

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
    logPartitionTable(diskNumber, log);

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
            const std::string mounts = mountPoints(name);
            const std::string volumeText = "Volume " + toUtf8(path) + (mounts.empty() ? "" : " (" + mounts + ")");
            bool locked = false;
            DWORD lockError = 0;
            int attempts = 0;
            for (int attempt = 0; attempt < 10 && !locked; ++attempt) {
                ++attempts;
                locked = DeviceIoControl(v, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr) != FALSE;
                if (!locked) {
                    lockError = GetLastError();
                    Sleep(200);
                }
            }
            if (!locked) {
                log(volumeText + ": nicht sperrbar nach " + std::to_string(attempts) + " Versuchen (" + winErrorText(lockError) + ")");
                err = "Laufwerk in Benutzung – Explorer-Fenster/Programme schließen (" + winErrorText(lockError) + ")";
                CloseHandle(v);
                FindVolumeClose(find);
                return nullptr;
            }
            d->volumes_.push_back(v);
            if (!DeviceIoControl(v, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr)) {
                err = "Volume konnte nicht ausgehängt werden: " + winErrorText(GetLastError());
                log(volumeText + ": gesperrt, " + err);
                FindVolumeClose(find);
                return nullptr;
            }
            log(volumeText + ": gesperrt nach " + std::to_string(attempts) + " Versuch(en), ausgehängt");
        } while (FindNextVolumeW(find, name, MAX_PATH));
        FindVolumeClose(find);
    }
    if (d->volumes_.empty()) log("Keine eingehängten Volumes auf dem Laufwerk");

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
    log("Laufwerk roh geöffnet: \\\\.\\PhysicalDrive" + std::to_string(diskNumber) + ", " + groupDigits(d->size_) +
        " Bytes, Sektorgröße " + std::to_string(d->sector_) + " Bytes");
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
