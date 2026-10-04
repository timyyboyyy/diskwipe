// Läuft ohne Adminrechte: listDrives() öffnet Laufwerke nur zur Abfrage (Zugriffsrecht 0).
#include <cstdio>

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

TEST(win_system_disk_never_selectable) {
    for (const DriveInfo& d : listDrives())
        if (d.system) CHECK(!isSelectable(d, true));
}
