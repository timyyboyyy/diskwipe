#include "device_file.h"
#include "memory_device.h"
#include "test.h"

using namespace dw;

TEST(file_device_reports_size_and_sector) {
    const std::string path = tmpPath("dev_size.img");
    writeFile(path, 10240, 0);
    FileDevice d(path);
    CHECK_EQ(d.size(), uint64_t(10240));
    CHECK_EQ(d.sectorSize(), uint32_t(512));
}

TEST(file_device_roundtrip) {
    const std::string path = tmpPath("dev_roundtrip.img");
    writeFile(path, 8192, 0);
    {
        FileDevice d(path);
        std::vector<uint8_t> w(512, 0xAB), r(512, 0);
        CHECK(d.write(1024, w.data(), w.size()));
        CHECK(d.flush());
        CHECK(d.read(1024, r.data(), r.size()));
        CHECK(r == w);
    }
    const std::vector<uint8_t> file = readFile(path);
    CHECK_EQ(int(file[1023]), 0);
    CHECK_EQ(int(file[1024]), 0xAB);
    CHECK_EQ(int(file[1535]), 0xAB);
    CHECK_EQ(int(file[1536]), 0);
}

TEST(file_device_rejects_out_of_range) {
    const std::string path = tmpPath("dev_range.img");
    writeFile(path, 4096, 0);
    FileDevice d(path);
    std::vector<uint8_t> buf(512);
    CHECK(!d.read(4000, buf.data(), buf.size()));
    CHECK(!d.lastError().empty());
    CHECK(!d.write(4096, buf.data(), 1));
}

TEST(file_device_missing_file_throws) {
    bool threw = false;
    try {
        FileDevice d(tmpPath("does_not_exist.img"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(memory_device_fault_injection) {
    MemoryDevice m(4096);
    m.failWriteAt = 1000;
    std::vector<uint8_t> buf(512, 1);
    CHECK(m.write(0, buf.data(), 512));
    CHECK(!m.write(512, buf.data(), 512));
    CHECK(!m.lastError().empty());
}

TEST(memory_device_fake_capacity_wraps) {
    MemoryDevice m(4096, 512, 1024);
    std::vector<uint8_t> w(512, 7), r(512, 0);
    CHECK(m.write(2048, w.data(), 512));  // landet physisch bei 0
    CHECK(m.read(0, r.data(), 512));
    CHECK(r == w);
}
