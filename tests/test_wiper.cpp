#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>

#include "device_file.h"
#include "memory_device.h"
#include "test.h"
#include "wiper.h"

using namespace dw;

namespace {

std::vector<PassSpec> standardPlan() {
    return {{PatternKind::Random, 0x1111}, {PatternKind::Random, 0x2222}, {PatternKind::Random, 0x3333}, {PatternKind::Zero, 0}};
}

bool allZero(const std::vector<uint8_t>& v) {
    return std::all_of(v.begin(), v.end(), [](uint8_t x) { return x == 0; });
}

void pokeByte(const std::string& path, uint64_t offset, uint8_t value) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(offset));
    f.put(static_cast<char>(value));
}

}  // namespace

TEST(make_plan_ends_with_zero_and_uses_fresh_seeds) {
    uint64_t next = 100;
    const auto plan = makePlan(3, [&] { return next++; });
    CHECK_EQ(plan.size(), size_t(4));
    for (int i = 0; i < 3; ++i) {
        CHECK(plan[i].kind == PatternKind::Random);
        CHECK_EQ(plan[i].seed, uint64_t(100 + i));
    }
    CHECK(plan[3].kind == PatternKind::Zero);
}

TEST(make_plan_without_random_passes_is_zero_only) {
    const auto plan = makePlan(0, [] { return uint64_t(1); });
    CHECK_EQ(plan.size(), size_t(1));
    CHECK(plan[0].kind == PatternKind::Zero);
}

TEST(wipe_file_with_odd_size_is_all_zero) {
    const std::string path = tmpPath("wipe_odd.img");
    const uint64_t size = 3 * kBlockSize + 3 * 512;
    writeFile(path, size, 0xAB);
    std::atomic<bool> cancel{false};
    {
        FileDevice d(path);
        const Result r = runPasses(d, standardPlan(), nullptr, cancel);
        CHECK(r.status == Status::Success);
        CHECK_EQ(r.bytesTotal, size);
        CHECK_EQ(r.passesCompleted, 4);
        const Result v = runVerifyZero(d, nullptr, cancel);
        CHECK(v.status == Status::Success);
    }
    const auto data = readFile(path);
    CHECK_EQ(uint64_t(data.size()), size);
    CHECK(allZero(data));
}

TEST(run_wipe_uses_secure_seeds_and_succeeds) {
    MemoryDevice m(2 * kBlockSize + 512);
    std::atomic<bool> cancel{false};
    const Result r = runWipe(m, 2, nullptr, cancel);
    CHECK(r.status == Status::Success);
    CHECK_EQ(r.passesCompleted, 3);
    CHECK(allZero(m.data()));
}

TEST(verify_detects_single_changed_byte) {
    const std::string path = tmpPath("verify_single.img");
    const uint64_t offset = kBlockSize + 17;
    writeFile(path, 2 * kBlockSize + 512, 0);
    pokeByte(path, offset, 0x01);
    FileDevice d(path);
    std::atomic<bool> cancel{false};
    const Result r = runVerifyZero(d, nullptr, cancel);
    CHECK(r.status == Status::VerifyMismatch);
    CHECK_EQ(r.failedPass, 1);
    CHECK_EQ(r.firstMismatch, offset);
    CHECK_EQ(r.mismatchCount, uint64_t(1));
    CHECK(!r.excerpt.empty());
    CHECK_EQ(int(r.excerpt[0]), 0x01);
}

TEST(verify_counts_mismatches_across_blocks) {
    const std::string path = tmpPath("verify_multi.img");
    writeFile(path, 3 * kBlockSize, 0);
    pokeByte(path, 5, 0xFF);
    pokeByte(path, kBlockSize + 100, 0xFF);
    pokeByte(path, 2 * kBlockSize + 200, 0xFF);
    FileDevice d(path);
    std::atomic<bool> cancel{false};
    const Result r = runVerifyZero(d, nullptr, cancel);
    CHECK(r.status == Status::VerifyMismatch);
    CHECK_EQ(r.mismatchCount, uint64_t(3));
    CHECK_EQ(r.firstMismatch, uint64_t(5));
}

TEST(verify_excerpt_at_device_end_is_truncated) {
    const std::string path = tmpPath("verify_end.img");
    const uint64_t size = kBlockSize + 512;
    writeFile(path, size, 0);
    pokeByte(path, size - 10, 0x7F);
    FileDevice d(path);
    std::atomic<bool> cancel{false};
    const Result r = runVerifyZero(d, nullptr, cancel);
    CHECK(r.status == Status::VerifyMismatch);
    CHECK_EQ(r.firstMismatch, size - 10);
    CHECK_EQ(r.excerpt.size(), size_t(10));
    CHECK_EQ(int(r.excerpt[0]), 0x7F);
}

TEST(write_error_reports_io_error) {
    MemoryDevice m(2 * kBlockSize);
    m.failWriteAt = kBlockSize + 5;
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, standardPlan(), nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK_EQ(r.failedPass, 1);
    CHECK_EQ(r.errorOffset, uint64_t(kBlockSize));
    CHECK(r.message.find("simulierter Schreibfehler") != std::string::npos);
}

TEST(read_error_during_verify_reports_io_error) {
    MemoryDevice m(2 * kBlockSize);
    m.failReadAt = 100;
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, standardPlan(), nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK_EQ(r.errorOffset, uint64_t(0));
}

TEST(fake_capacity_is_detected) {
    MemoryDevice m(4 * kBlockSize, 512, kBlockSize);
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, standardPlan(), nullptr, cancel);
    CHECK(r.status == Status::VerifyMismatch);
    CHECK_EQ(r.failedPass, 1);
}

TEST(cancel_during_write_reports_cancelled) {
    MemoryDevice m(4 * kBlockSize);
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, standardPlan(), [&](const Progress&) { cancel = true; }, cancel);
    CHECK(r.status == Status::Cancelled);
    CHECK_EQ(r.failedPass, 1);
}

TEST(cancel_during_verify_reports_cancelled) {
    MemoryDevice m(4 * kBlockSize);
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, {{PatternKind::Zero, 0}},
                               [&](const Progress& p) { if (p.phase == Phase::Verify) cancel = true; }, cancel);
    CHECK(r.status == Status::Cancelled);
}

TEST(sector_4096_device_with_odd_size_is_fully_covered) {
    MemoryDevice m(kBlockSize + 3 * 4096, 4096);
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, {{PatternKind::Random, 7}, {PatternKind::Zero, 0}}, nullptr, cancel);
    CHECK(r.status == Status::Success);
    CHECK(allZero(m.data()));
}

TEST(progress_reaches_total_in_every_phase) {
    MemoryDevice m(2 * kBlockSize + 512);
    std::atomic<bool> cancel{false};
    std::map<std::pair<int, int>, Progress> last;
    const Result r = runPasses(m, {{PatternKind::Random, 1}, {PatternKind::Zero, 0}},
                               [&](const Progress& p) { last[{p.pass, int(p.phase)}] = p; }, cancel);
    CHECK(r.status == Status::Success);
    CHECK_EQ(last.size(), size_t(4));
    for (const auto& kv : last) {
        CHECK_EQ(kv.second.done, kv.second.total);
        CHECK_EQ(kv.second.totalPasses, 2);
    }
}

TEST(zero_size_device_is_io_error) {
    MemoryDevice m(0);
    std::atomic<bool> cancel{false};
    CHECK(runPasses(m, standardPlan(), nullptr, cancel).status == Status::IoError);
    CHECK(runVerifyZero(m, nullptr, cancel).status == Status::IoError);
}

TEST(empty_plan_is_io_error) {
    MemoryDevice m(kBlockSize);
    std::atomic<bool> cancel{false};
    CHECK(runPasses(m, {}, nullptr, cancel).status == Status::IoError);
}
