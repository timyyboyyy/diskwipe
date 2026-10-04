#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "device.h"
#include "pattern.h"

namespace dw {

enum class Phase { Write, Verify };

struct Progress {
    int pass = 0;         // 1-basiert
    int totalPasses = 0;
    Phase phase = Phase::Write;
    uint64_t done = 0;    // Bytes in dieser Phase
    uint64_t total = 0;   // Größe des Datenträgers
};

using ProgressFn = std::function<void(const Progress&)>;

enum class Status { Success, Cancelled, IoError, VerifyMismatch };

struct Result {
    Status status = Status::Success;
    int failedPass = 0;            // 1-basiert, 0 = keiner
    int passesCompleted = 0;       // vollständig geschrieben und verifiziert
    uint64_t bytesTotal = 0;
    uint64_t errorOffset = 0;
    uint64_t firstMismatch = 0;
    uint64_t mismatchCount = 0;
    std::vector<uint8_t> excerpt;  // bis zu 32 tatsächlich gelesene Bytes ab firstMismatch
    std::string message;
};

struct PassSpec {
    PatternKind kind;
    uint64_t seed;
};

// randomPasses Zufallsdurchgänge mit je eigenem Seed, danach immer ein Nulldurchgang.
std::vector<PassSpec> makePlan(int randomPasses, const std::function<uint64_t()>& seedSource);

// Jeder Durchgang: komplett schreiben, flushen, komplett zurücklesen und vergleichen.
Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const ProgressFn& progress,
                 const std::atomic<bool>& cancel);

// makePlan mit secureRandomSeed + runPasses.
Result runWipe(BlockDevice& dev, int randomPasses, const ProgressFn& progress, const std::atomic<bool>& cancel);

// Nur lesen: prüft, dass jedes Byte 0x00 ist.
Result runVerifyZero(BlockDevice& dev, const ProgressFn& progress, const std::atomic<bool>& cancel);

}  // namespace dw
