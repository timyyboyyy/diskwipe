#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
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

// Nach so vielen Blöcken wird in der Schreibphase geflusht; erst dann gilt der Offset als Checkpoint.
constexpr uint64_t kCheckpointBlocks = 64;

// Stelle, an der ein unterbrochener Löschvorgang weitergeht.
struct ResumePoint {
    int pass = 1;                 // 1-basiert
    Phase phase = Phase::Write;
    uint64_t offset = 0;          // blockausgerichtet; Write: letzter Checkpoint
    uint64_t writtenEnd = 0;      // Write: exklusives Ende aller in diesem Durchgang versuchten Schreibzugriffe
    uint64_t mismatches = 0;      // Verify: bisher gezählte Abweichungen
    uint64_t firstMismatch = 0;
    std::vector<uint8_t> excerpt;
};

enum class EventKind { PhaseStarted, PhaseCompleted };

struct Event {
    EventKind kind;
    int pass;             // 1-basiert
    int totalPasses;
    Phase phase;
    PatternKind pattern;
    uint64_t offset;      // PhaseStarted: Start-Offset; PhaseCompleted: Größe
    uint64_t mismatches;  // PhaseCompleted in der Prüfphase
};

using EventFn = std::function<void(const Event&)>;

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
    std::optional<ResumePoint> resume;  // nur bei IoError aus runPasses: hier kann fortgesetzt werden
};

struct PassSpec {
    PatternKind kind;
    uint64_t seed;
};

struct RunOptions {
    ResumePoint start;                            // Standard: Durchgang 1, Schreiben, Offset 0
    EventFn events;                               // optional
    uint64_t checkpointBlocks = kCheckpointBlocks;
};

// randomPasses Zufallsdurchgänge mit je eigenem Seed, danach immer ein Nulldurchgang.
std::vector<PassSpec> makePlan(int randomPasses, const std::function<uint64_t()>& seedSource);

// Jeder Durchgang: komplett schreiben, flushen, komplett zurücklesen und vergleichen.
Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const ProgressFn& progress,
                 const std::atomic<bool>& cancel);

// Wie oben, aber ab opts.start und mit Ereignissen. Bei IoError ist Result::resume gesetzt.
Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const RunOptions& opts, const ProgressFn& progress,
                 const std::atomic<bool>& cancel);

// makePlan mit secureRandomSeed + runPasses.
Result runWipe(BlockDevice& dev, int randomPasses, const ProgressFn& progress, const std::atomic<bool>& cancel);

// Nur lesen: prüft, dass jedes Byte 0x00 ist. Nicht fortsetzbar.
Result runVerifyZero(BlockDevice& dev, const ProgressFn& progress, const std::atomic<bool>& cancel,
                     const EventFn& events = nullptr);

}  // namespace dw
