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
    CHECK_EQ(describeDrive(d), std::string("Disk 2 – - – SanDisk Cruzer – 29,8 GB – USB"));
    d.usb = false;
    d.removable = true;
    d.model = "";
    CHECK_EQ(describeDrive(d), std::string("Disk 2 – - – Unbekannt – 29,8 GB – Wechseldatenträger"));
    d.removable = false;
    CHECK_EQ(describeDrive(d), std::string("Disk 2 – - – Unbekannt – 29,8 GB – Intern"));
}

TEST(tiny_drive_is_not_selectable) {
    CHECK(!isSelectable(drive(true, true, false, 1024), false));
    CHECK(!isSelectable(drive(true, true, false, 1024), true));
    CHECK(isSelectable(drive(true, false, false, kBlockSize), false));
}

TEST(describe_drive_lists_volumes) {
    DriveInfo d = drive(true, false, false, 15728640000ULL);
    d.number = 3;
    d.model = "VendorCo ProductCode";
    d.volumes = {"E: (MEINSTICK)", "F:"};
    CHECK_EQ(describeDrive(d), std::string("Disk 3 – E: (MEINSTICK), F: – VendorCo ProductCode – 14,6 GB – USB"));
    d.volumes.clear();
    CHECK_EQ(describeDrive(d), std::string("Disk 3 – - – VendorCo ProductCode – 14,6 GB – USB"));
    d.volumes = {"C:", "D:"};
    d.usb = false;
    CHECK_EQ(describeDrive(d), std::string("Disk 3 – C:, D: – VendorCo ProductCode – 14,6 GB – Intern"));
}

namespace {
DriveInfo stick(int number, const std::string& serial) {
    DriveInfo d;
    d.number = number;
    d.model = "Intenso Rainbow";
    d.serial = serial;
    d.size = 15728640000ULL;
    d.usb = true;
    d.removable = true;
    return d;
}
}  // namespace

TEST(describe_drives_marks_duplicates_with_serial) {
    DriveInfo other = drive(true, false, false, 8ULL << 30);
    other.number = 4;
    other.model = "Other";
    other.serial = "CCCC3333";
    const auto r = describeDrives({stick(1, "AAAA1111"), stick(2, "BBBB2222"), other});
    CHECK_EQ(r.size(), size_t(3));
    CHECK_EQ(r[0], std::string("Disk 1 – - – Intenso Rainbow [SN …1111] – 14,6 GB – USB"));
    CHECK_EQ(r[1], std::string("Disk 2 – - – Intenso Rainbow [SN …2222] – 14,6 GB – USB"));
    CHECK_EQ(r[2], std::string("Disk 4 – - – Other – 8,0 GB – USB"));
}

TEST(describe_drives_identical_serials_no_suffix) {
    const auto r = describeDrives({stick(1, "SAME0000"), stick(2, "SAME0000")});
    CHECK_EQ(r[0], std::string("Disk 1 – - – Intenso Rainbow – 14,6 GB – USB"));
    CHECK_EQ(r[1], std::string("Disk 2 – - – Intenso Rainbow – 14,6 GB – USB"));
}

TEST(describe_drives_empty_serial_no_suffix) {
    const auto r = describeDrives({stick(1, ""), stick(2, "BBBB2222")});
    CHECK_EQ(r[0], std::string("Disk 1 – - – Intenso Rainbow – 14,6 GB – USB"));
    CHECK_EQ(r[1], std::string("Disk 2 – - – Intenso Rainbow [SN …2222] – 14,6 GB – USB"));
}

TEST(identity_ignores_disk_number) {
    const std::vector<DriveInfo> list = {stick(4, "AA001234")};
    const auto hits = findByIdentity(list, stick(3, "AA001234"));
    CHECK_EQ(hits.size(), size_t(1));
    CHECK_EQ(hits[0], size_t(0));
}

TEST(identity_distinguishes_twin_sticks_by_serial) {
    const std::vector<DriveInfo> list = {stick(4, "AA001234"), stick(5, "AA009999")};
    const auto hits = findByIdentity(list, stick(3, "AA009999"));
    CHECK_EQ(hits.size(), size_t(1));
    CHECK_EQ(hits[0], size_t(1));
}

TEST(identity_with_duplicate_serial_is_ambiguous) {
    const std::vector<DriveInfo> list = {stick(4, "0000"), stick(5, "0000")};
    CHECK_EQ(findByIdentity(list, stick(3, "0000")).size(), size_t(2));
}

TEST(identity_without_serial_never_matches) {
    const std::vector<DriveInfo> list = {stick(4, "")};
    CHECK(findByIdentity(list, stick(3, "")).empty());
    CHECK(!sameIdentity(stick(3, ""), stick(3, "")));
}

TEST(identity_requires_same_model_size_and_bus) {
    DriveInfo other = stick(4, "AA001234");
    other.size += 512;
    CHECK(!sameIdentity(other, stick(3, "AA001234")));
    other = stick(4, "AA001234");
    other.model = "Andere";
    CHECK(!sameIdentity(other, stick(3, "AA001234")));
    other = stick(4, "AA001234");
    other.usb = false;
    CHECK(!sameIdentity(other, stick(3, "AA001234")));
}

TEST(identity_never_matches_system_disk) {
    DriveInfo sys = stick(0, "AA001234");
    sys.system = true;
    CHECK(findByIdentity({sys}, stick(3, "AA001234")).empty());
}

TEST(drive_kind_names) {
    DriveInfo d = stick(1, "x");
    CHECK_EQ(std::string(driveKind(d)), std::string("USB"));
    d.usb = false;
    CHECK_EQ(std::string(driveKind(d)), std::string("Wechseldatenträger"));
    d.removable = false;
    CHECK_EQ(std::string(driveKind(d)), std::string("Intern"));
}
