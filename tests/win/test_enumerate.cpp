// Läuft ohne Adminrechte: listDrives() öffnet Laufwerke nur zur Abfrage (Zugriffsrecht 0).
#include <windows.h>

#include <cstdio>
#include <string>

#include "drive.h"
#include "enumerate_win.h"
#include "test.h"

using namespace dw;

TEST(win_list_drives_finds_system_disk) {
    const auto drives = listDrives();
    CHECK(!drives.empty());
    bool anySystem = false;
    for (const DriveInfo& d : drives) {
        std::printf("  gefunden: %s%s\n", describeDrive(d).c_str(), d.system ? " [System]" : "");
        anySystem = anySystem || d.system;
    }
    CHECK(anySystem);
}

TEST(win_query_drive_matches_list) {
    for (const DriveInfo& d : listDrives()) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(d.number);
        HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        if (h == INVALID_HANDLE_VALUE) continue;
        const DriveInfo q = queryDrive(h, d.number);
        CloseHandle(h);
        CHECK(q.model == d.model);
        CHECK(q.serial == d.serial);
        CHECK(q.size == d.size);
        CHECK(q.usb == d.usb);
        CHECK(q.removable == d.removable);
    }
}

TEST(win_system_disk_never_selectable) {
    for (const DriveInfo& d : listDrives())
        if (d.system) CHECK(!isSelectable(d, true));
}
