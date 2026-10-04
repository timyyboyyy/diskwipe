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

Result runPlan(BlockDevice& dev, const std::vector<PassSpec>& plan, bool doWrite, const ProgressFn& progress,
               const std::atomic<bool>& cancel) {
    const uint64_t total = dev.size();
    const int passes = static_cast<int>(plan.size());

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
    auto report = [&](int pass, Phase phase, uint64_t done) {
        if (progress) progress(Progress{pass, passes, phase, done, total});
    };

    if (total == 0) return fail(Status::IoError, 0, 0, "Kapazität ist 0 oder nicht ermittelbar");
    if (plan.empty()) return fail(Status::IoError, 0, 0, "Leerer Löschplan");

    AlignedBuffer expected(kBlockSize), actual(kBlockSize);
    Result ok;
    ok.bytesTotal = total;

    for (int i = 0; i < passes; ++i) {
        const int pass = i + 1;
        const PassSpec& spec = plan[i];

        if (doWrite) {
            uint64_t block = 0;
            for (uint64_t off = 0; off < total; off += kBlockSize, ++block) {
                if (cancel) return fail(Status::Cancelled, pass, off, kCancelledMsg);
                const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlockSize, total - off));
                fillPattern(expected.get(), n, spec.kind, spec.seed, block);
                if (!dev.write(off, expected.get(), n))
                    return fail(Status::IoError, pass, off,
                                "Schreibfehler bei Offset " + std::to_string(off) + ": " + dev.lastError());
                report(pass, Phase::Write, off + n);
            }
            if (!dev.flush()) return fail(Status::IoError, pass, total, "Flush fehlgeschlagen: " + dev.lastError());
        }

        uint64_t mismatches = 0, first = 0;
        std::vector<uint8_t> excerpt;
        uint64_t block = 0;
        for (uint64_t off = 0; off < total; off += kBlockSize, ++block) {
            if (cancel) return fail(Status::Cancelled, pass, off, kCancelledMsg);
            const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlockSize, total - off));
            if (!dev.read(off, actual.get(), n))
                return fail(Status::IoError, pass, off,
                            "Lesefehler bei Offset " + std::to_string(off) + ": " + dev.lastError());
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
    return runPlan(dev, plan, true, progress, cancel);
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

Result runVerifyZero(BlockDevice& dev, const ProgressFn& progress, const std::atomic<bool>& cancel) {
    return runPlan(dev, {{PatternKind::Zero, 0}}, false, progress, cancel);
}

}  // namespace dw
