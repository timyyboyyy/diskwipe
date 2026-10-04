#pragma once
#include <windows.h>

#include <vector>

#include "drive.h"

namespace dw {

// Alle physischen Laufwerke (\\.\PhysicalDrive0..63). Benötigt keine Adminrechte.
// Kann die Systemplatte nicht ermittelt werden, gilt jedes nicht-USB-/nicht-Wechsel-Laufwerk als system.
std::vector<DriveInfo> listDrives();

// Fragt Modell/Seriennummer/USB/Wechselmedium/Größe über ein bereits geöffnetes Handle ab (system bleibt false).
DriveInfo queryDrive(HANDLE h, int number);

}  // namespace dw
