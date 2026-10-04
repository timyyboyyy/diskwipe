#pragma once
#include <vector>

#include "drive.h"

namespace dw {

// Alle physischen Laufwerke (\\.\PhysicalDrive0..63). Benötigt keine Adminrechte.
// Kann die Systemplatte nicht ermittelt werden, gilt jedes nicht-USB-/nicht-Wechsel-Laufwerk als system.
std::vector<DriveInfo> listDrives();

}  // namespace dw
