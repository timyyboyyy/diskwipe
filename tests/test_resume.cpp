#include <algorithm>
#include <atomic>

#include "memory_device.h"
#include "test.h"
#include "wiper.h"

using namespace dw;

namespace {

std::vector<PassSpec> twoRandomPlusZero() {
    return {{PatternKind::Random, 0xA1}, {PatternKind::Random, 0xB2}, {PatternKind::Zero, 0}};
}

bool allZero(const std::vector<uint8_t>& v) {
    return std::all_of(v.begin(), v.end(), [](uint8_t x) { return x == 0; });
}

RunOptions every2Blocks(const ResumePoint& start = ResumePoint{}, EventFn events = nullptr) {
    RunOptions o;
    o.start = start;
    o.events = std::move(events);
    o.checkpointBlocks = 2;
    return o;
}

// Fehler ab Durchgang `pass` scharf schalten (davor laufen die Durchgänge sauber durch).
EventFn armWriteFaultInPass(MemoryDevice& m, int pass, uint64_t offset) {
    return [&m, pass, offset](const Event& e) {
        if (e.kind == EventKind::PhaseStarted && e.pass == pass && e.phase == Phase::Write) m.failWriteAt = offset;
    };
}

}  // namespace

TEST(write_error_in_pass_2_resumes_from_last_checkpoint) {
    MemoryDevice m(7 * kBlockSize + 512);
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, twoRandomPlusZero(), every2Blocks(ResumePoint{}, armWriteFaultInPass(m, 2, 5 * kBlockSize + 10)),
                               nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK_EQ(r.failedPass, 2);
    CHECK_EQ(r.passesCompleted, 1);
    CHECK_EQ(r.errorOffset, uint64_t(5 * kBlockSize));
    CHECK(r.resume.has_value());
    CHECK_EQ(r.resume->pass, 2);
    CHECK(r.resume->phase == Phase::Write);
    CHECK_EQ(r.resume->offset, uint64_t(4 * kBlockSize));
    CHECK_EQ(r.resume->writtenEnd, uint64_t(6 * kBlockSize));

    m.failWriteAt = MemoryDevice::kNoFault;
    const Result again = runPasses(m, twoRandomPlusZero(), every2Blocks(*r.resume), nullptr, cancel);
    CHECK(again.status == Status::Success);
    CHECK_EQ(again.passesCompleted, 3);
    CHECK(!again.resume.has_value());
    CHECK(allZero(m.data()));
}

TEST(resume_after_lost_write_cache_still_succeeds) {
    MemoryDevice m(7 * kBlockSize + 512);
    m.loseUnflushedOnFault = true;
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, twoRandomPlusZero(), every2Blocks(ResumePoint{}, armWriteFaultInPass(m, 2, 5 * kBlockSize + 10)),
                               nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK(r.resume.has_value());
    m.failWriteAt = MemoryDevice::kNoFault;
    m.loseUnflushedOnFault = false;
    const Result again = runPasses(m, twoRandomPlusZero(), every2Blocks(*r.resume), nullptr, cancel);
    CHECK(again.status == Status::Success);
    CHECK(allZero(m.data()));
}

TEST(read_error_in_verify_resumes_at_block) {
    MemoryDevice m(4 * kBlockSize);
    m.failReadAt = 2 * kBlockSize + 7;
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, twoRandomPlusZero(), every2Blocks(), nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK(r.resume.has_value());
    CHECK_EQ(r.resume->pass, 1);
    CHECK(r.resume->phase == Phase::Verify);
    CHECK_EQ(r.resume->offset, uint64_t(2 * kBlockSize));

    m.failReadAt = MemoryDevice::kNoFault;
    const Result again = runPasses(m, twoRandomPlusZero(), every2Blocks(*r.resume), nullptr, cancel);
    CHECK(again.status == Status::Success);
    CHECK(allZero(m.data()));
}

TEST(mismatches_before_interruption_are_carried_over) {
    MemoryDevice m(4 * kBlockSize);
    m.failReadAt = 2 * kBlockSize;
    std::atomic<bool> cancel{false};
    bool poked = false;
    const EventFn poke = [&](const Event& e) {
        if (!poked && e.kind == EventKind::PhaseStarted && e.phase == Phase::Verify) {
            m.data()[17] = 0x01;
            poked = true;
        }
    };
    const std::vector<PassSpec> zeroOnly = {{PatternKind::Zero, 0}};
    const Result r = runPasses(m, zeroOnly, every2Blocks(ResumePoint{}, poke), nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK(r.resume.has_value());
    CHECK_EQ(r.resume->mismatches, uint64_t(1));
    CHECK_EQ(r.resume->firstMismatch, uint64_t(17));

    m.failReadAt = MemoryDevice::kNoFault;
    const Result again = runPasses(m, zeroOnly, every2Blocks(*r.resume), nullptr, cancel);
    CHECK(again.status == Status::VerifyMismatch);
    CHECK_EQ(again.mismatchCount, uint64_t(1));
    CHECK_EQ(again.firstMismatch, uint64_t(17));
    CHECK(!again.excerpt.empty());
    CHECK_EQ(int(again.excerpt[0]), 0x01);
}

TEST(write_phase_flushes_at_every_checkpoint) {
    MemoryDevice m(9 * kBlockSize);
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, {{PatternKind::Zero, 0}}, every2Blocks(), nullptr, cancel);
    CHECK(r.status == Status::Success);
    CHECK_EQ(m.flushCount, 5);  // nach Block 1, 3, 5, 7 und am Phasenende
}

TEST(default_checkpoint_interval_is_64_mib) {
    CHECK_EQ(kCheckpointBlocks, uint64_t(64));
    CHECK_EQ(RunOptions{}.checkpointBlocks, kCheckpointBlocks);
}

TEST(flush_failure_at_phase_end_resumes_from_last_checkpoint) {
    MemoryDevice m(3 * kBlockSize);
    m.failFlushNumber = 2;  // 1 = Checkpoint nach Block 1, 2 = Phasenende
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, twoRandomPlusZero(), every2Blocks(), nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK(r.resume.has_value());
    CHECK_EQ(r.resume->pass, 1);
    CHECK(r.resume->phase == Phase::Write);
    CHECK_EQ(r.resume->offset, uint64_t(2 * kBlockSize));
    CHECK_EQ(r.resume->writtenEnd, uint64_t(3 * kBlockSize));

    m.failFlushNumber = -1;
    const Result again = runPasses(m, twoRandomPlusZero(), every2Blocks(*r.resume), nullptr, cancel);
    CHECK(again.status == Status::Success);
}

TEST(second_interruption_keeps_maximum_written_end) {
    MemoryDevice m(7 * kBlockSize + 512);
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, twoRandomPlusZero(), every2Blocks(ResumePoint{}, armWriteFaultInPass(m, 2, 5 * kBlockSize + 10)),
                               nullptr, cancel);
    CHECK(r.resume.has_value());
    m.failWriteAt = 4 * kBlockSize + 3;  // diesmal weiter vorn
    const Result again = runPasses(m, twoRandomPlusZero(), every2Blocks(*r.resume), nullptr, cancel);
    CHECK(again.status == Status::IoError);
    CHECK(again.resume.has_value());
    CHECK_EQ(again.resume->offset, uint64_t(4 * kBlockSize));
    CHECK_EQ(again.resume->writtenEnd, uint64_t(6 * kBlockSize));
}

TEST(events_report_every_phase_in_order) {
    MemoryDevice m(2 * kBlockSize);
    std::atomic<bool> cancel{false};
    std::vector<Event> seen;
    const Result r = runPasses(m, {{PatternKind::Random, 1}, {PatternKind::Zero, 0}},
                               every2Blocks(ResumePoint{}, [&](const Event& e) { seen.push_back(e); }), nullptr, cancel);
    CHECK(r.status == Status::Success);
    CHECK_EQ(seen.size(), size_t(8));
    const Phase phases[] = {Phase::Write, Phase::Write, Phase::Verify, Phase::Verify};
    for (size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i].kind == (i % 2 == 0 ? EventKind::PhaseStarted : EventKind::PhaseCompleted));
        CHECK_EQ(seen[i].pass, int(i / 4) + 1);
        CHECK_EQ(seen[i].totalPasses, 2);
        CHECK(seen[i].phase == phases[i % 4]);
        CHECK(seen[i].pattern == (i < 4 ? PatternKind::Random : PatternKind::Zero));
    }
}

TEST(resumed_run_starts_with_event_at_resume_offset) {
    MemoryDevice m(4 * kBlockSize);
    std::fill(m.data().begin(), m.data().end(), uint8_t(0));  // Blöcke vor dem Fortsetzungspunkt gelten als bereits geschrieben
    std::atomic<bool> cancel{false};
    ResumePoint start;
    start.pass = 1;
    start.phase = Phase::Write;
    start.offset = 2 * kBlockSize;
    std::vector<Event> seen;
    const Result r = runPasses(m, {{PatternKind::Zero, 0}}, every2Blocks(start, [&](const Event& e) { seen.push_back(e); }),
                               nullptr, cancel);
    CHECK(r.status == Status::Success);
    CHECK(!seen.empty());
    CHECK(seen[0].kind == EventKind::PhaseStarted);
    CHECK_EQ(seen[0].offset, uint64_t(2 * kBlockSize));
}

TEST(invalid_resume_point_is_rejected) {
    MemoryDevice m(4 * kBlockSize);
    std::atomic<bool> cancel{false};
    ResumePoint unaligned;
    unaligned.offset = 100;
    const Result a = runPasses(m, twoRandomPlusZero(), every2Blocks(unaligned), nullptr, cancel);
    CHECK(a.status == Status::IoError);
    CHECK(!a.resume.has_value());
    ResumePoint badPass;
    badPass.pass = 4;
    CHECK(runPasses(m, twoRandomPlusZero(), every2Blocks(badPass), nullptr, cancel).status == Status::IoError);
    ResumePoint pastEnd;
    pastEnd.offset = 4 * kBlockSize;
    CHECK(runPasses(m, twoRandomPlusZero(), every2Blocks(pastEnd), nullptr, cancel).status == Status::IoError);
}

TEST(verify_zero_read_error_is_not_resumable) {
    MemoryDevice m(2 * kBlockSize);
    m.failReadAt = kBlockSize;
    std::atomic<bool> cancel{false};
    const Result r = runVerifyZero(m, nullptr, cancel);
    CHECK(r.status == Status::IoError);
    CHECK(!r.resume.has_value());
}

TEST(cancel_and_mismatch_are_not_resumable) {
    MemoryDevice m(4 * kBlockSize);
    std::atomic<bool> cancel{false};
    const Result c = runPasses(m, twoRandomPlusZero(), [&](const Progress&) { cancel = true; }, cancel);
    CHECK(c.status == Status::Cancelled);
    CHECK(!c.resume.has_value());
    cancel = false;
    MemoryDevice fake(4 * kBlockSize, 512, kBlockSize);
    const Result v = runPasses(fake, twoRandomPlusZero(), nullptr, cancel);
    CHECK(v.status == Status::VerifyMismatch);
    CHECK(!v.resume.has_value());
}
