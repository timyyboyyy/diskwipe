#include "wiper.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace dw {
namespace {

constexpr size_t kAlignment = 4096;
constexpr size_t kExcerptLen = 32;
const char* const kCancelledMsg = "Abgebrochen – Datenträger unvollständig gelöscht";

// Sektorausgerichteter Puffer (nötig für FILE_FLAG_NO_BUFFERING unter Windows).
class AlignedBuffer {
public:
    explicit AlignedBuffer(size_t size) {
#ifdef _WIN32
        data_ = static_cast<uint8_t*>(_aligned_malloc(size, kAlignment));
#else
        void* p = nullptr;
        if (posix_memalign(&p, kAlignment, size) == 0) data_ = static_cast<uint8_t*>(p);
#endif
        if (!data_) throw std::bad_alloc();
    }
    ~AlignedBuffer() {
#ifdef _WIN32
        _aligned_free(data_);
#else
        std::free(data_);
#endif
    }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    uint8_t* get() const { return data_; }

private:
    uint8_t* data_ = nullptr;
};

Result runPlan(BlockDevice& dev, const std::vector<PassSpec>& plan, bool doWrite, const RunOptions& opts,
               const ProgressFn& progress, const std::atomic<bool>& cancel) {
    const uint64_t total = dev.size();
    const int passes = static_cast<int>(plan.size());
    const ResumePoint& start = opts.start;
    const uint64_t checkpointBlocks = opts.checkpointBlocks > 0 ? opts.checkpointBlocks : kCheckpointBlocks;

    auto fail = [&](Status status, int pass, uint64_t offset, std::string message) {
        Result f;
        f.status = status;
        f.failedPass = pass;
        f.passesCompleted = pass > 0 ? pass - 1 : 0;
        f.bytesTotal = total;
        f.errorOffset = offset;
        f.message = std::move(message);
        return f;
    };
    // I/O-Fehler, ab `at` fortsetzbar.
    auto interrupted = [&](int pass, uint64_t offset, std::string message, ResumePoint at) {
        Result f = fail(Status::IoError, pass, offset, std::move(message));
        f.resume = std::move(at);
        return f;
    };
    auto report = [&](int pass, Phase phase, uint64_t done) {
        if (progress) progress(Progress{pass, passes, phase, done, total});
    };
    auto emit = [&](EventKind kind, int pass, Phase phase, uint64_t offset, uint64_t mismatches) {
        if (opts.events) opts.events(Event{kind, pass, passes, phase, plan[pass - 1].kind, offset, mismatches});
    };

    if (total == 0) return fail(Status::IoError, 0, 0, "Kapazität ist 0 oder nicht ermittelbar");
    if (plan.empty()) return fail(Status::IoError, 0, 0, "Leerer Löschplan");
    if (start.pass < 1 || start.pass > passes || start.offset % kBlockSize != 0 ||
        (start.offset > 0 && start.offset >= total) || (start.phase == Phase::Write && !doWrite))
        return fail(Status::IoError, 0, 0, "Ungültiger Fortsetzungspunkt");

    AlignedBuffer expected(kBlockSize), actual(kBlockSize);
    Result ok;
    ok.bytesTotal = total;
    ok.passesCompleted = start.pass - 1;

    for (int i = start.pass - 1; i < passes; ++i) {
        const int pass = i + 1;
        const PassSpec& spec = plan[i];
        const bool resuming = pass == start.pass;

        if (doWrite && !(resuming && start.phase == Phase::Verify)) {
            const uint64_t from = resuming ? start.offset : 0;
            uint64_t checkpoint = from;  // bis hierhin geschrieben und geflusht
            uint64_t writtenEnd = resuming ? std::max(start.writtenEnd, from) : 0;
            auto writeFail = [&](uint64_t offset, std::string message) {
                ResumePoint at;
                at.pass = pass;
                at.phase = Phase::Write;
                at.offset = checkpoint;
                at.writtenEnd = writtenEnd;
                return interrupted(pass, offset, std::move(message), std::move(at));
            };
            emit(EventKind::PhaseStarted, pass, Phase::Write, from, 0);
            uint64_t block = from / kBlockSize;
            for (uint64_t off = from; off < total; off += kBlockSize, ++block) {
                if (cancel) return fail(Status::Cancelled, pass, off, kCancelledMsg);
                const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlockSize, total - off));
                fillPattern(expected.get(), n, spec.kind, spec.seed, block);
                writtenEnd = std::max(writtenEnd, off + n);  // auch ein fehlgeschlagener Zugriff kann teilweise landen
                if (!dev.write(off, expected.get(), n))
                    return writeFail(off, "Schreibfehler bei Offset " + std::to_string(off) + ": " + dev.lastError());
                report(pass, Phase::Write, off + n);
                if ((block + 1) % checkpointBlocks == 0 && off + n < total) {
                    if (!dev.flush()) return writeFail(off + n, "Flush fehlgeschlagen: " + dev.lastError());
                    checkpoint = off + n;
                }
            }
            if (!dev.flush()) return writeFail(total, "Flush fehlgeschlagen: " + dev.lastError());
            emit(EventKind::PhaseCompleted, pass, Phase::Write, total, 0);
        }

        const bool resumeVerify = resuming && start.phase == Phase::Verify;
        const uint64_t from = resumeVerify ? start.offset : 0;
        uint64_t mismatches = resumeVerify ? start.mismatches : 0;
        uint64_t first = resumeVerify ? start.firstMismatch : 0;
        std::vector<uint8_t> excerpt = resumeVerify ? start.excerpt : std::vector<uint8_t>{};
        emit(EventKind::PhaseStarted, pass, Phase::Verify, from, 0);
        uint64_t block = from / kBlockSize;
        for (uint64_t off = from; off < total; off += kBlockSize, ++block) {
            if (cancel) return fail(Status::Cancelled, pass, off, kCancelledMsg);
            const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlockSize, total - off));
            if (!dev.read(off, actual.get(), n)) {
                ResumePoint at;
                at.pass = pass;
                at.phase = Phase::Verify;
                at.offset = off;
                at.mismatches = mismatches;
                at.firstMismatch = first;
                at.excerpt = excerpt;
                return interrupted(pass, off, "Lesefehler bei Offset " + std::to_string(off) + ": " + dev.lastError(),
                                   std::move(at));
            }
            fillPattern(expected.get(), n, spec.kind, spec.seed, block);
            if (std::memcmp(expected.get(), actual.get(), n) != 0) {
                for (size_t j = 0; j < n; ++j) {
                    if (expected.get()[j] == actual.get()[j]) continue;
                    if (mismatches == 0) {
                        first = off + j;
                        const size_t len = std::min(kExcerptLen, n - j);
                        excerpt.assign(actual.get() + j, actual.get() + j + len);
                    }
                    ++mismatches;
                }
            }
            report(pass, Phase::Verify, off + n);
        }
        emit(EventKind::PhaseCompleted, pass, Phase::Verify, total, mismatches);
        if (mismatches > 0) {
            Result f = fail(Status::VerifyMismatch, pass, first,
                            std::to_string(mismatches) + " abweichende Bytes, erste bei Offset " + std::to_string(first));
            f.firstMismatch = first;
            f.mismatchCount = mismatches;
            f.excerpt = std::move(excerpt);
            return f;
        }
        ok.passesCompleted = pass;
    }
    ok.status = Status::Success;
    return ok;
}

}  // namespace

std::vector<PassSpec> makePlan(int randomPasses, const std::function<uint64_t()>& seedSource) {
    std::vector<PassSpec> plan;
    for (int i = 0; i < randomPasses; ++i) plan.push_back({PatternKind::Random, seedSource()});
    plan.push_back({PatternKind::Zero, 0});
    return plan;
}

Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const ProgressFn& progress,
                 const std::atomic<bool>& cancel) {
    return runPlan(dev, plan, true, RunOptions{}, progress, cancel);
}

Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const RunOptions& opts, const ProgressFn& progress,
                 const std::atomic<bool>& cancel) {
    return runPlan(dev, plan, true, opts, progress, cancel);
}

Result runWipe(BlockDevice& dev, int randomPasses, const ProgressFn& progress, const std::atomic<bool>& cancel) {
    std::vector<PassSpec> plan;
    try {
        plan = makePlan(randomPasses, secureRandomSeed);
    } catch (const std::exception& e) {
        Result r;
        r.status = Status::IoError;
        r.message = std::string("Zufallsquelle nicht verfügbar: ") + e.what();
        return r;
    }
    return runPasses(dev, plan, progress, cancel);
}

Result runVerifyZero(BlockDevice& dev, const ProgressFn& progress, const std::atomic<bool>& cancel, const EventFn& events) {
    RunOptions opts;
    opts.start.phase = Phase::Verify;
    opts.events = events;
    Result r = runPlan(dev, {{PatternKind::Zero, 0}}, false, opts, progress, cancel);
    r.resume.reset();  // reines Prüfen wird nicht fortgesetzt
    return r;
}



ContentCheck checkResumeContent(BlockDevice& dev, const std::vector<PassSpec>& plan, const ResumePoint& at, std::string& err) {
    const uint64_t total = dev.size();
    if (total == 0 || at.pass < 1 || at.pass > static_cast<int>(plan.size())) {
        err = "Ungültiger Fortsetzungspunkt";
        return ContentCheck::ReadError;
    }
    const uint64_t lastStart = (total - 1) / kBlockSize * kBlockSize;
    const PassSpec& current = plan[at.pass - 1];

    struct Probe {
        uint64_t offset;
        const PassSpec* spec;
    };
    std::vector<Probe> probes;
    if (at.phase == Phase::Verify) {
        probes.push_back({0, &current});
        if (lastStart > 0) probes.push_back({lastStart, &current});
    } else {
        if (at.offset > 0) probes.push_back({0, &current});
        if (at.pass > 1 && at.writtenEnd <= lastStart) probes.push_back({lastStart, &plan[at.pass - 2]});
    }

    AlignedBuffer expected(kBlockSize), actual(kBlockSize);
    bool strong = false;
    for (const Probe& p : probes) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlockSize, total - p.offset));
        if (!dev.read(p.offset, actual.get(), n)) {
            err = "Lesefehler bei Offset " + std::to_string(p.offset) + ": " + dev.lastError();
            return ContentCheck::ReadError;
        }
        fillPattern(expected.get(), n, p.spec->kind, p.spec->seed, p.offset / kBlockSize);
        if (std::memcmp(expected.get(), actual.get(), n) != 0) return ContentCheck::Mismatch;
        if (p.spec->kind == PatternKind::Random) strong = true;
    }
    return strong ? ContentCheck::Strong : ContentCheck::Weak;
}

}  // namespace dw
