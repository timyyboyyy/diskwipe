#include "drive.h"
#include "pattern.h"
#include "test.h"

using namespace dw;

namespace {
DriveInfo drive(bool usb, bool removable, bool system, uint64_t size = 8ULL << 30) {
    DriveInfo d;
    d.number = 1;
    d.model = "Test";
    d.size = size;
    d.usb = usb;
    d.removable = removable;
    d.system = system;
    return d;
}
}  // namespace

TEST(system_disk_is_never_selectable) {
    CHECK(!isSelectable(drive(false, false, true), false));
    CHECK(!isSelectable(drive(false, false, true), true));
    CHECK(!isSelectable(drive(true, true, true), true));
}

TEST(usb_and_removable_are_selectable_by_default) {
    CHECK(isSelectable(drive(true, false, false), false));
    CHECK(isSelectable(drive(false, true, false), false));
}

TEST(internal_disk_needs_opt_in) {
    CHECK(!isSelectable(drive(false, false, false), false));
    CHECK(isSelectable(drive(false, false, false), true));
}

TEST(zero_size_drive_is_not_selectable) {
    CHECK(!isSelectable(drive(true, true, false, 0), true));
}

TEST(format_bytes_uses_binary_units_and_german_comma) {
    CHECK_EQ(formatBytes(512), std::string("512 B"));
    CHECK_EQ(formatBytes(1536), std::string("1,5 KB"));
    CHECK_EQ(formatBytes(32010928128ULL), std::string("29,8 GB"));
}

TEST(describe_drive_format) {
    DriveInfo d = drive(true, false, false, 32010928128ULL);
    d.number = 2;
    d.model = "SanDisk Cruzer";
    CHECK_EQ(describeDrive(d), std::string("Disk 2 – SanDisk Cruzer – 29,8 GB – USB"));
    d.usb = false;
    d.removable = true;
    d.model = "";
    CHECK_EQ(describeDrive(d), std::string("Disk 2 – Unbekannt – 29,8 GB – Wechseldatenträger"));
    d.removable = false;
    CHECK_EQ(describeDrive(d), std::string("Disk 2 – Unbekannt – 29,8 GB – Intern"));
}

TEST(tiny_drive_is_not_selectable) {
    CHECK(!isSelectable(drive(true, true, false, 1024), false));
    CHECK(!isSelectable(drive(true, true, false, 1024), true));
    CHECK(isSelectable(drive(true, false, false, kBlockSize), false));
}
