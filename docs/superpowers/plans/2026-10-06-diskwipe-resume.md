# diskwipe Fortsetzen & Protokoll Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein beim Löschen abgestecktes Laufwerk kann nach dem Wiedereinstecken an der unterbrochenen Stelle fortgesetzt werden; jeder Lauf erzeugt eine Protokolldatei, die am Ende per "Speichern unter" angeboten wird.

**Architecture:** Der portable Kern (`wiper`) lernt Checkpoints, Start ab `ResumePoint` und Phasen-Ereignisse und kann prüfen, ob ein Datenträger zum unterbrochenen Vorgang passt. Identitätsabgleich (`drive`) und Protokoll (`audit_log`) sind ebenfalls portabel und unter Linux getestet. Die GUI hält den Fortsetzen-Stand im Speicher, erkennt das Laufwerk beim Wiedereinstecken und startet den Worker ab dem Checkpoint.

**Tech Stack:** C++17, MinGW-w64 (posix), Win32 (GetSaveFileNameW, SHGetKnownFolderPath, CopyFileW), eigenes Test-Framework (`tests/test.h`), Make.

**Spec:** `docs/superpowers/specs/2026-10-06-diskwipe-resume-design.md`

## Global Constraints

- Alle Global Constraints aus `docs/superpowers/plans/2026-10-04-diskwipe.md` und `docs/superpowers/plans/2026-10-04-diskwipe-ux.md` gelten weiter (C++17, MinGW posix, statisch, deutsche Texte, W-APIs, Sicherheitsregeln).
- Tests dürfen niemals `WinPhysicalDevice::open` aufrufen oder ein Laufwerk mit Schreibzugriff öffnen; `diskwipe.exe` und Installer werden nicht gestartet.
- Checkpoint-Intervall: 64 Blöcke à 1 MiB (`kCheckpointBlocks = 64`). Ein Offset gilt erst nach erfolgreichem `flush()` als Checkpoint.
- Kein Fortsetzen ohne Seriennummer; Fortsetzen nur bei genau einem passenden Laufwerk (Modell, Seriennummer, Größe, USB, Removable; Nummer egal; nie Systemplatte).
- Seeds stehen nie im Protokoll. Keine Prüfsumme im Protokoll.
- Protokollzeile: `YYYY-MM-DD HH:MM:SS  text` + CRLF, UTF-8, nach jeder Zeile auf den Datenträger geschrieben.
- Temp-Protokoll: `%TEMP%\diskwipe-<YYYYMMDD-HHMMSS>-<n>.log`; Vorschlag beim Speichern: `diskwipe_<YYYY-MM-DD_HH-MM-SS>_<Modell>_SN<letzte 4>.log` im Ordner "Dokumente".
- Fortsetzen-Button: Text "Fortsetzen", Position (336, 106), Größe 100×30.
- Die Felder heißen im Code `writtenEnd` (exklusives Ende aller in diesem Durchgang versuchten Schreibzugriffe). Die Spec nennt es `failedAt`; gemeint ist dasselbe Konzept, `writtenEnd` ist präziser, weil es über mehrere Unterbrechungen das Maximum hält.

## Review Focus

1. **Abstecken genau beim Checkpoint-Flush oder beim Flush am Phasenende** → Fortsetzen ab dem vorherigen Checkpoint, nicht ab dem Phasenende. Test: `flush_failure_at_phase_end_resumes_from_last_checkpoint` (Task 2).
2. **Zweite Unterbrechung im fortgesetzten Lauf, weiter vorn als die erste** → `writtenEnd` bleibt das Maximum, damit die Inhaltsprüfung den bereits angefassten letzten Block nicht für "unberührt" hält. Test: `second_interruption_keeps_maximum_written_end` (Task 2).
3. **Winziger Datenträger (ein einziger, kurzer Block)** → Inhaltsprüfung liest keinen Block doppelt und meldet Weak statt fälschlich Strong/Mismatch. Test: `content_check_single_block_device_is_weak` (Task 3).
4. **Temp-Pfad mit Umlauten** (z.B. Benutzername "Jürgen") → Protokoll lässt sich trotzdem anlegen. Test: `audit_log_handles_utf8_path` (Task 5, läuft auch in `make test-win`).
5. **Abstecken während "Prüfen" (nur lesen)** → kein Fortsetzen-Stand, endgültiger Fehler, Protokoll abgeschlossen. Test: `verify_zero_read_error_is_not_resumable` (Task 2); GUI-Prüfung manuell (Task 8, Schritt 28).

---

## Dateistruktur

| Datei | Verantwortung | Task |
|---|---|---|
| `tests/memory_device.h` | Test-Datenträger: Flush-Zähler, Flush-Fehler, Verlust nicht geflushter Schreibzugriffe | 1 |
| `src/wiper.{h,cpp}` | `ResumePoint`, `RunOptions`, `Event`, Checkpoints, Start ab Punkt | 2 |
| `src/wiper.{h,cpp}` | `ContentCheck`, `checkResumeContent` | 3 |
| `src/drive.{h,cpp}` | `driveKind` öffentlich, `sameIdentity`, `findByIdentity` | 4 |
| `src/audit_log.{h,cpp}` (neu) | Zeilenformat, Texte für Ereignisse/Unterbrechung, `AuditLog`-Datei | 5 |
| `src/gui_win.cpp`, `Makefile` | Protokoll pro Lauf, Speichern-Dialog, `launchWorker` | 6 |
| `src/gui_win.cpp` | Fortsetzen-Zustand, Button, Erkennung, Verwerfen | 7 |
| `docs/manual-test.md`, `README.md` | manuelle Tests, Feature-Beschreibung | 8 |

Build-/Testbefehle (WSL, im Projektordner `~/diskwipe`):
- `make test-unit` – Linux-Unit-Tests; einzelne Tests: `./build/diskwipe_tests <Namensteil>`
- `make test-win` – dieselben Tests als Windows-Exe über WSL-Interop
- `make windows` – baut `build/diskwipe.exe` (nicht starten)

---

### Task 1: MemoryDevice simuliert Abstecken

**Files:**
- Modify: `tests/memory_device.h`
- Test: `tests/test_device.cpp` (anhängen)

**Interfaces:**
- Produces: `MemoryDevice::flushCount` (int, Anzahl `flush()`-Aufrufe), `MemoryDevice::failFlushNumber` (int, 1-basiert; dieser `flush()`-Aufruf schlägt fehl; `-1` = nie), `MemoryDevice::loseUnflushedOnFault` (bool; bei einem Schreibfehler werden alle seit dem letzten erfolgreichen Flush geschriebenen Bytes auf den vorherigen Inhalt zurückgesetzt).

- [ ] **Step 1: Failing tests schreiben** – an `tests/test_device.cpp` anhängen (Datei inkludiert bereits `memory_device.h` und `test.h`):

```cpp
TEST(memory_device_counts_and_fails_flushes) {
    MemoryDevice m(4096);
    m.failFlushNumber = 2;
    CHECK(m.flush());
    CHECK(!m.flush());
    CHECK(m.lastError().find("simulierter Flush-Fehler") != std::string::npos);
    CHECK(m.flush());
    CHECK_EQ(m.flushCount, 3);
}

TEST(memory_device_loses_unflushed_writes_on_fault) {
    MemoryDevice m(4096);
    m.loseUnflushedOnFault = true;
    const uint8_t a[512] = {1};
    const uint8_t b[512] = {2};
    CHECK(m.write(0, a, 512));
    CHECK(m.flush());
    CHECK(m.write(512, b, 512));
    CHECK_EQ(int(m.data()[512]), 2);
    m.failWriteAt = 1024;
    CHECK(!m.write(1024, b, 512));
    CHECK_EQ(int(m.data()[0]), 1);      // geflusht: bleibt
    CHECK_EQ(int(m.data()[512]), 0x5A); // nicht geflusht: verloren
}
```

- [ ] **Step 2: Tests laufen lassen, Fehlschlag prüfen**

Run: `make test-unit`
Expected: Kompilierfehler `'class MemoryDevice' has no member named 'failFlushNumber'`

- [ ] **Step 3: MemoryDevice erweitern** – `tests/memory_device.h` komplett ersetzen durch:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "device.h"

// Speicher-Datenträger für Tests. realSize < reportedSize simuliert einen Fake-Stick:
// Adressen werden modulo realSize abgebildet.
class MemoryDevice : public dw::BlockDevice {
public:
    static constexpr uint64_t kNoFault = UINT64_MAX;

    MemoryDevice(uint64_t reportedSize, uint32_t sectorSize = 512, uint64_t realSize = 0)
        : reported_(reportedSize), sector_(sectorSize), data_(realSize ? realSize : reportedSize, 0x5A) {}

    uint64_t failWriteAt = kNoFault;
    uint64_t failReadAt = kNoFault;
    int failFlushNumber = -1;           // 1-basiert: dieser flush()-Aufruf schlägt fehl
    bool loseUnflushedOnFault = false;  // Abstecken: Schreibcache des Sticks geht verloren
    int flushCount = 0;

    uint64_t size() const override { return reported_; }
    uint32_t sectorSize() const override { return sector_; }

    bool read(uint64_t off, uint8_t* buf, size_t n) override {
        if (hits(failReadAt, off, n)) return fail("simulierter Lesefehler");
        for (size_t i = 0; i < n; ++i) buf[i] = data_[(off + i) % data_.size()];
        return true;
    }
    bool write(uint64_t off, const uint8_t* buf, size_t n) override {
        if (hits(failWriteAt, off, n)) {
            if (loseUnflushedOnFault) rollback();
            return fail("simulierter Schreibfehler");
        }
        if (loseUnflushedOnFault) {
            std::vector<uint8_t> old(n);
            for (size_t i = 0; i < n; ++i) old[i] = data_[(off + i) % data_.size()];
            undo_.emplace_back(off, std::move(old));
        }
        for (size_t i = 0; i < n; ++i) data_[(off + i) % data_.size()] = buf[i];
        return true;
    }
    bool flush() override {
        ++flushCount;
        if (flushCount == failFlushNumber) {
            if (loseUnflushedOnFault) rollback();
            return fail("simulierter Flush-Fehler");
        }
        undo_.clear();
        return true;
    }
    std::string lastError() const override { return err_; }

    std::vector<uint8_t>& data() { return data_; }

private:
    static bool hits(uint64_t fault, uint64_t off, size_t n) { return fault != kNoFault && fault >= off && fault < off + n; }
    bool fail(const char* msg) {
        err_ = msg;
        return false;
    }
    void rollback() {
        for (auto it = undo_.rbegin(); it != undo_.rend(); ++it)
            for (size_t i = 0; i < it->second.size(); ++i) data_[(it->first + i) % data_.size()] = it->second[i];
        undo_.clear();
    }

    uint64_t reported_;
    uint32_t sector_;
    std::vector<uint8_t> data_;
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> undo_;
    std::string err_;
};
```

- [ ] **Step 4: Tests laufen lassen**

Run: `make test-unit`
Expected: alle Tests PASS, darunter `memory_device_counts_and_fails_flushes`, `memory_device_loses_unflushed_writes_on_fault`

- [ ] **Step 5: Commit**

```bash
git add tests/memory_device.h tests/test_device.cpp
git commit -m "test: MemoryDevice simulates flush failures and lost write cache"
```

---

### Task 2: wiper – Checkpoints, Fortsetzen, Ereignisse

**Files:**
- Modify: `src/wiper.h`, `src/wiper.cpp`
- Test: `tests/test_resume.cpp` (neu; das Makefile nimmt `tests/test_*.cpp` automatisch auf)

**Interfaces:**
- Consumes: `MemoryDevice` aus Task 1 (`failWriteAt`, `failReadAt`, `failFlushNumber`, `loseUnflushedOnFault`, `flushCount`).
- Produces (in `namespace dw`, `src/wiper.h`):
  - `constexpr uint64_t kCheckpointBlocks = 64;`
  - `struct ResumePoint { int pass = 1; Phase phase = Phase::Write; uint64_t offset = 0; uint64_t writtenEnd = 0; uint64_t mismatches = 0; uint64_t firstMismatch = 0; std::vector<uint8_t> excerpt; };`
  - `enum class EventKind { PhaseStarted, PhaseCompleted };`
  - `struct Event { EventKind kind; int pass; int totalPasses; Phase phase; PatternKind pattern; uint64_t offset; uint64_t mismatches; };`
  - `using EventFn = std::function<void(const Event&)>;`
  - `struct RunOptions { ResumePoint start; EventFn events; uint64_t checkpointBlocks = kCheckpointBlocks; };`
  - `Result::resume` vom Typ `std::optional<ResumePoint>` – nur bei `Status::IoError` aus `runPasses` gesetzt.
  - `Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const RunOptions& opts, const ProgressFn& progress, const std::atomic<bool>& cancel);`
  - bestehend, unverändert nutzbar: `runPasses(dev, plan, progress, cancel)`, `runWipe(...)`
  - `Result runVerifyZero(BlockDevice& dev, const ProgressFn& progress, const std::atomic<bool>& cancel, const EventFn& events = nullptr);` – `resume` ist nie gesetzt.

- [ ] **Step 1: Failing tests schreiben** – `tests/test_resume.cpp` anlegen:

```cpp
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
```

- [ ] **Step 2: Tests laufen lassen, Fehlschlag prüfen**

Run: `make test-unit`
Expected: Kompilierfehler `'RunOptions' was not declared in this scope`

- [ ] **Step 3: `src/wiper.h` erweitern** – `#include <optional>` ergänzen; nach `using ProgressFn = …;` einfügen:

```cpp
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
```

In `struct Result` nach `std::string message;` ergänzen:

```cpp
    std::optional<ResumePoint> resume;  // nur bei IoError aus runPasses: hier kann fortgesetzt werden
```

Nach `struct PassSpec {…};` ergänzen:

```cpp
struct RunOptions {
    ResumePoint start;                            // Standard: Durchgang 1, Schreiben, Offset 0
    EventFn events;                               // optional
    uint64_t checkpointBlocks = kCheckpointBlocks;
};
```

Deklarationen ersetzen/ergänzen:

```cpp
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
```

- [ ] **Step 4: `src/wiper.cpp` umbauen** – die Funktion `runPlan` vollständig ersetzen durch:

```cpp
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
```

Die öffentlichen Funktionen am Dateiende ersetzen durch:

```cpp
Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const ProgressFn& progress,
                 const std::atomic<bool>& cancel) {
    return runPlan(dev, plan, true, RunOptions{}, progress, cancel);
}

Result runPasses(BlockDevice& dev, const std::vector<PassSpec>& plan, const RunOptions& opts, const ProgressFn& progress,
                 const std::atomic<bool>& cancel) {
    return runPlan(dev, plan, true, opts, progress, cancel);
}
```

(`runWipe` bleibt unverändert.)

```cpp
Result runVerifyZero(BlockDevice& dev, const ProgressFn& progress, const std::atomic<bool>& cancel, const EventFn& events) {
    RunOptions opts;
    opts.start.phase = Phase::Verify;
    opts.events = events;
    Result r = runPlan(dev, {{PatternKind::Zero, 0}}, false, opts, progress, cancel);
    r.resume.reset();  // reines Prüfen wird nicht fortgesetzt
    return r;
}
```

- [ ] **Step 5: Tests laufen lassen**

Run: `make test-unit`
Expected: alle Tests PASS (alte Wiper-Tests unverändert grün, neue Tests aus `test_resume.cpp` grün), keine Compiler-Warnungen.

- [ ] **Step 6: Commit**

```bash
git add src/wiper.h src/wiper.cpp tests/test_resume.cpp
git commit -m "feat: checkpoints and resume points in wiper"
```

---

### Task 3: wiper – Inhaltsprüfung vor dem Fortsetzen

**Files:**
- Modify: `src/wiper.h`, `src/wiper.cpp`
- Test: `tests/test_resume.cpp` (anhängen)

**Interfaces:**
- Consumes: `ResumePoint`, `RunOptions`, `runPasses(dev, plan, opts, progress, cancel)` aus Task 2.
- Produces:
  - `enum class ContentCheck { Strong, Weak, Mismatch, ReadError };`
  - `ContentCheck checkResumeContent(BlockDevice& dev, const std::vector<PassSpec>& plan, const ResumePoint& at, std::string& err);` – liest höchstens zwei Blöcke; `err` wird nur bei `ReadError` gesetzt.

Regeln (aus der Spec):
- `lastStart = ((size − 1) / kBlockSize) · kBlockSize`.
- Phase Verify: Block 0 muss Muster von Durchgang `at.pass` haben; ist `lastStart > 0`, auch der letzte Block.
- Phase Write: ist `at.offset > 0`, muss Block 0 Muster von `at.pass` haben. Ist `at.pass > 1` und `at.writtenEnd ≤ lastStart`, muss der letzte Block Muster von `at.pass − 1` haben.
- Lesefehler → `ReadError`; eine Abweichung → `Mismatch`; mindestens ein Treffer gegen ein Zufallsmuster → `Strong`; sonst `Weak`.

- [ ] **Step 1: Failing tests anhängen** an `tests/test_resume.cpp`:

```cpp
namespace {

// Lauf in Durchgang 2 bei Block 5 unterbrechen (7,5 Blöcke groß, Checkpoint alle 2 Blöcke).
ResumePoint interruptPass2(MemoryDevice& m) {
    std::atomic<bool> cancel{false};
    const Result r = runPasses(m, twoRandomPlusZero(), every2Blocks(ResumePoint{}, armWriteFaultInPass(m, 2, 5 * kBlockSize + 10)),
                               nullptr, cancel);
    if (!r.resume) throw TestFailure("Unterbrechung erwartet");
    m.failWriteAt = MemoryDevice::kNoFault;
    return *r.resume;
}

}  // namespace

TEST(content_check_same_device_is_strong) {
    MemoryDevice m(7 * kBlockSize + 512);
    const ResumePoint at = interruptPass2(m);
    std::string err;
    CHECK(checkResumeContent(m, twoRandomPlusZero(), at, err) == ContentCheck::Strong);
}

TEST(content_check_foreign_device_is_mismatch) {
    MemoryDevice m(7 * kBlockSize + 512);
    const ResumePoint at = interruptPass2(m);
    MemoryDevice foreign(7 * kBlockSize + 512);  // gleiche Größe, Fremdinhalt 0x5A
    std::string err;
    CHECK(checkResumeContent(foreign, twoRandomPlusZero(), at, err) == ContentCheck::Mismatch);
}

TEST(content_check_nothing_written_yet_is_weak) {
    MemoryDevice m(4 * kBlockSize);
    ResumePoint at;  // Durchgang 1, Checkpoint 0
    at.writtenEnd = kBlockSize;
    std::string err;
    CHECK(checkResumeContent(m, twoRandomPlusZero(), at, err) == ContentCheck::Weak);
}

TEST(content_check_zero_pass_with_overwritten_last_block_is_weak) {
    MemoryDevice m(4 * kBlockSize);
    std::atomic<bool> cancel{false};
    CHECK(runPasses(m, twoRandomPlusZero(), nullptr, cancel).status == Status::Success);
    ResumePoint at;
    at.pass = 3;
    at.offset = 2 * kBlockSize;
    at.writtenEnd = 4 * kBlockSize;
    std::string err;
    CHECK(checkResumeContent(m, twoRandomPlusZero(), at, err) == ContentCheck::Weak);
}

TEST(content_check_zero_pass_before_last_block_is_strong) {
    MemoryDevice m(6 * kBlockSize);
    std::atomic<bool> cancel{false};
    const std::vector<PassSpec> plan = {{PatternKind::Random, 7}, {PatternKind::Zero, 0}};
    const Result r = runPasses(m, plan, every2Blocks(ResumePoint{}, armWriteFaultInPass(m, 2, 3 * kBlockSize)), nullptr, cancel);
    CHECK(r.resume.has_value());
    CHECK_EQ(r.resume->offset, uint64_t(2 * kBlockSize));
    std::string err;
    CHECK(checkResumeContent(m, plan, *r.resume, err) == ContentCheck::Strong);
    MemoryDevice zeroed(6 * kBlockSize);
    std::fill(zeroed.data().begin(), zeroed.data().end(), 0);
    CHECK(checkResumeContent(zeroed, plan, *r.resume, err) == ContentCheck::Mismatch);
}

TEST(content_check_verify_phase) {
    MemoryDevice m(4 * kBlockSize);
    std::atomic<bool> cancel{false};
    const std::vector<PassSpec> plan = {{PatternKind::Random, 9}, {PatternKind::Zero, 0}};
    CHECK(runPasses(m, {plan[0]}, nullptr, cancel).status == Status::Success);  // Durchgang 1 komplett
    ResumePoint at;
    at.pass = 1;
    at.phase = Phase::Verify;
    at.offset = 2 * kBlockSize;
    std::string err;
    CHECK(checkResumeContent(m, plan, at, err) == ContentCheck::Strong);
    MemoryDevice foreign(4 * kBlockSize);
    CHECK(checkResumeContent(foreign, plan, at, err) == ContentCheck::Mismatch);
    MemoryDevice zeroed(4 * kBlockSize);
    std::fill(zeroed.data().begin(), zeroed.data().end(), 0);
    at.pass = 2;  // Nulldurchgang prüfen: nur schwacher Nachweis
    CHECK(checkResumeContent(zeroed, plan, at, err) == ContentCheck::Weak);
}

TEST(content_check_single_block_device_is_weak) {
    MemoryDevice m(kBlockSize / 2);
    std::atomic<bool> cancel{false};
    CHECK(runPasses(m, twoRandomPlusZero(), nullptr, cancel).status == Status::Success);
    ResumePoint at;
    at.pass = 2;
    at.offset = 0;
    at.writtenEnd = kBlockSize / 2;  // der einzige Block wurde in Durchgang 2 schon angefasst
    std::string err;
    CHECK(checkResumeContent(m, twoRandomPlusZero(), at, err) == ContentCheck::Weak);
}

TEST(content_check_read_error) {
    MemoryDevice m(7 * kBlockSize + 512);
    const ResumePoint at = interruptPass2(m);
    m.failReadAt = 0;
    std::string err;
    CHECK(checkResumeContent(m, twoRandomPlusZero(), at, err) == ContentCheck::ReadError);
    CHECK(err.find("simulierter Lesefehler") != std::string::npos);
}
```

- [ ] **Step 2: Tests laufen lassen, Fehlschlag prüfen**

Run: `make test-unit`
Expected: Kompilierfehler `'checkResumeContent' was not declared in this scope`

- [ ] **Step 3: Deklaration in `src/wiper.h`** am Ende des Namespace ergänzen:

```cpp
enum class ContentCheck {
    Strong,    // Zufallsmuster des Vorgangs gefunden: sicher derselbe Datenträger
    Weak,      // nichts Widersprüchliches, aber kein Zufallsmuster prüfbar
    Mismatch,  // Inhalt passt nicht zum unterbrochenen Vorgang
    ReadError, // err ist gesetzt
};

// Prüft vor dem ersten Schreibzugriff, ob dev zum unterbrochenen Vorgang passt. Liest höchstens zwei Blöcke.
ContentCheck checkResumeContent(BlockDevice& dev, const std::vector<PassSpec>& plan, const ResumePoint& at, std::string& err);
```

- [ ] **Step 4: Implementierung in `src/wiper.cpp`** nach `runVerifyZero` (innerhalb `namespace dw`) einfügen:

```cpp
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
```

- [ ] **Step 5: Tests laufen lassen**

Run: `make test-unit`
Expected: alle Tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/wiper.h src/wiper.cpp tests/test_resume.cpp
git commit -m "feat: content check before resuming on a re-plugged drive"
```

---

### Task 4: drive – Identitätsabgleich

**Files:**
- Modify: `src/drive.h`, `src/drive.cpp`
- Test: `tests/test_drive.cpp` (anhängen)

**Interfaces:**
- Produces:
  - `const char* driveKind(const DriveInfo& d);` – `"USB"`, `"Wechseldatenträger"` oder `"Intern"` (bisher anonym in `drive.cpp`, wird öffentlich).
  - `bool sameIdentity(const DriveInfo& a, const DriveInfo& b);` – Modell, Seriennummer, Größe, USB, Removable gleich und Seriennummer nicht leer; Nummer egal.
  - `std::vector<size_t> findByIdentity(const std::vector<DriveInfo>& list, const DriveInfo& id);` – Indizes aller Nicht-System-Laufwerke mit `sameIdentity`.

- [ ] **Step 1: Failing tests anhängen** an `tests/test_drive.cpp`:

```cpp
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
```

- [ ] **Step 2: Tests laufen lassen, Fehlschlag prüfen**

Run: `make test-unit`
Expected: Kompilierfehler `'findByIdentity' was not declared in this scope`

- [ ] **Step 3: `src/drive.h`** – vor `}  // namespace dw` ergänzen:

```cpp
const char* driveKind(const DriveInfo& d);  // "USB", "Wechseldatenträger" oder "Intern"

// Derselbe physische Datenträger (Nummer egal): Modell, Seriennummer, Größe, USB, Removable gleich.
// Ohne Seriennummer ist die Identität nie gesichert.
bool sameIdentity(const DriveInfo& a, const DriveInfo& b);
// Indizes aller Nicht-System-Laufwerke mit derselben Identität wie id.
std::vector<size_t> findByIdentity(const std::vector<DriveInfo>& list, const DriveInfo& id);
```

- [ ] **Step 4: `src/drive.cpp`** – die Zeile `const char* driveKind(const DriveInfo& d) { … }` aus dem anonymen Namespace entfernen und vor `namespace {` einfügen; am Ende (vor `}  // namespace dw`) ergänzen:

```cpp
const char* driveKind(const DriveInfo& d) { return d.usb ? "USB" : d.removable ? "Wechseldatenträger" : "Intern"; }
```

```cpp
bool sameIdentity(const DriveInfo& a, const DriveInfo& b) {
    return !a.serial.empty() && a.serial == b.serial && a.model == b.model && a.size == b.size && a.usb == b.usb &&
           a.removable == b.removable;
}

std::vector<size_t> findByIdentity(const std::vector<DriveInfo>& list, const DriveInfo& id) {
    std::vector<size_t> hits;
    for (size_t i = 0; i < list.size(); ++i)
        if (!list[i].system && sameIdentity(list[i], id)) hits.push_back(i);
    return hits;
}
```

- [ ] **Step 5: Tests laufen lassen**

Run: `make test-unit`
Expected: alle Tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/drive.h src/drive.cpp tests/test_drive.cpp
git commit -m "feat: identify a re-plugged drive by model, serial and size"
```

---

### Task 5: audit_log – Protokolldatei und Texte

**Files:**
- Create: `src/audit_log.h`, `src/audit_log.cpp` (werden über `CORE_SRC` automatisch in alle Builds aufgenommen)
- Test: `tests/test_audit_log.cpp` (neu)

**Interfaces:**
- Consumes: `Event`, `EventKind`, `ResumePoint`, `Result`, `ContentCheck` (Tasks 2–3); `DriveInfo`, `driveKind` (Task 4).
- Produces (`namespace dw`):
  - `std::string formatLogLine(const std::tm& t, const std::string& text);` → `"2026-10-06 14:32:05  text\r\n"`
  - `std::string groupDigits(uint64_t v);` → `"15.728.640.000"`
  - `std::string driveLogText(const DriveInfo& d);` → `"Disk 3 – Intenso Rainbow – SN AA001234 – 15.728.640.000 Bytes – USB"` (leere SN: `SN unbekannt`, leeres Modell: `Unbekannt`)
  - `std::string suggestLogName(const std::tm& t, const DriveInfo& d);` → `"diskwipe_2026-10-06_14-32-05_Intenso_Rainbow_SN1234.log"`
  - `std::string eventText(const Event& e);`
  - `std::string interruptionText(const Result& r, int totalPasses);` (erwartet `r.resume`)
  - `std::string resumedText(const ResumePoint& p, int totalPasses);`
  - `std::string contentCheckText(ContentCheck c);`
  - `std::tm localNow();`
  - `class AuditLog` mit `bool open(const std::string& utf8Path)`, `bool isOpen() const`, `const std::string& path() const`, `bool line(const std::string& text)` (thread-sicher, Zeitstempel, auf Datenträger geschrieben), `void close()`.

- [ ] **Step 1: Failing tests schreiben** – `tests/test_audit_log.cpp`:

```cpp
#include <ctime>
#include <string>

#include "audit_log.h"
#include "test.h"

using namespace dw;

namespace {

std::tm sampleTime() {
    std::tm t{};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 9;
    t.tm_mday = 6;
    t.tm_hour = 14;
    t.tm_min = 32;
    t.tm_sec = 5;
    return t;
}

DriveInfo sampleDrive() {
    DriveInfo d;
    d.number = 3;
    d.model = "Intenso Rainbow";
    d.serial = "AA001234";
    d.size = 15728640000ULL;
    d.usb = true;
    d.removable = true;
    return d;
}

std::string fileText(const std::string& path) {
    const auto bytes = readFile(path);
    return std::string(bytes.begin(), bytes.end());
}

}  // namespace

TEST(log_line_format) {
    CHECK_EQ(formatLogLine(sampleTime(), "Hallo"), std::string("2026-10-06 14:32:05  Hallo\r\n"));
}

TEST(group_digits) {
    CHECK_EQ(groupDigits(0), std::string("0"));
    CHECK_EQ(groupDigits(999), std::string("999"));
    CHECK_EQ(groupDigits(1000), std::string("1.000"));
    CHECK_EQ(groupDigits(15728640000ULL), std::string("15.728.640.000"));
}

TEST(drive_log_text) {
    DriveInfo d = sampleDrive();
    CHECK_EQ(driveLogText(d), std::string("Disk 3 – Intenso Rainbow – SN AA001234 – 15.728.640.000 Bytes – USB"));
    d.serial.clear();
    d.model.clear();
    CHECK_EQ(driveLogText(d), std::string("Disk 3 – Unbekannt – SN unbekannt – 15.728.640.000 Bytes – USB"));
}

TEST(suggested_log_name_is_a_valid_file_name) {
    DriveInfo d = sampleDrive();
    CHECK_EQ(suggestLogName(sampleTime(), d), std::string("diskwipe_2026-10-06_14-32-05_Intenso_Rainbow_SN1234.log"));
    d.model = "A/B:C*?\"<>|";
    d.serial = "12";
    CHECK_EQ(suggestLogName(sampleTime(), d), std::string("diskwipe_2026-10-06_14-32-05_A_B_C_______SN12.log"));
    d.serial.clear();
    d.model.clear();
    CHECK_EQ(suggestLogName(sampleTime(), d), std::string("diskwipe_2026-10-06_14-32-05_Unbekannt.log"));
}

TEST(event_texts) {
    CHECK_EQ(eventText(Event{EventKind::PhaseStarted, 1, 4, Phase::Write, PatternKind::Random, 0, 0}),
             std::string("Start Durchgang 1/4 (Zufall), Schreiben ab Offset 0"));
    CHECK_EQ(eventText(Event{EventKind::PhaseStarted, 4, 4, Phase::Verify, PatternKind::Zero, 1048576, 0}),
             std::string("Start Durchgang 4/4 (Nullen), Prüfen ab Offset 1.048.576"));
    CHECK_EQ(eventText(Event{EventKind::PhaseCompleted, 2, 4, Phase::Write, PatternKind::Random, 0, 0}),
             std::string("Ende Durchgang 2/4, Schreiben"));
    CHECK_EQ(eventText(Event{EventKind::PhaseCompleted, 2, 4, Phase::Verify, PatternKind::Random, 0, 3}),
             std::string("Ende Durchgang 2/4, Prüfen: 3 Abweichungen"));
}

TEST(interruption_and_resume_texts) {
    Result r;
    r.status = Status::IoError;
    r.message = "Schreibfehler bei Offset 6012534784: Das Gerät ist nicht bereit.";
    ResumePoint p;
    p.pass = 2;
    p.phase = Phase::Write;
    p.offset = 5972688896ULL;
    r.resume = p;
    CHECK_EQ(interruptionText(r, 4),
             std::string("Unterbrechung in Durchgang 2/4 (Schreiben): Schreibfehler bei Offset 6012534784: "
                         "Das Gerät ist nicht bereit.; weiter ab Checkpoint 5.972.688.896"));
    CHECK_EQ(resumedText(p, 4), std::string("Fortgesetzt in Durchgang 2/4, Schreiben ab Offset 5.972.688.896"));
    p.phase = Phase::Verify;
    r.resume = p;
    CHECK_EQ(interruptionText(r, 4),
             std::string("Unterbrechung in Durchgang 2/4 (Prüfen): Schreibfehler bei Offset 6012534784: "
                         "Das Gerät ist nicht bereit.; weiter ab Offset 5.972.688.896"));
}

TEST(content_check_texts) {
    CHECK_EQ(contentCheckText(ContentCheck::Strong), std::string("Inhaltsprüfung: passt (Zufallsmuster des Vorgangs gefunden)"));
    CHECK_EQ(contentCheckText(ContentCheck::Weak),
             std::string("Inhaltsprüfung: keine eindeutige Zuordnung möglich – Bestätigung erforderlich"));
    CHECK_EQ(contentCheckText(ContentCheck::Mismatch), std::string("Inhaltsprüfung: Inhalt passt nicht zum unterbrochenen Vorgang"));
    CHECK_EQ(contentCheckText(ContentCheck::ReadError), std::string("Inhaltsprüfung: Lesefehler"));
}

TEST(audit_log_writes_lines_immediately) {
    const std::string path = tmpPath("audit.log");
    AuditLog log;
    CHECK(log.open(path));
    CHECK(log.isOpen());
    CHECK_EQ(log.path(), path);
    CHECK(log.line("Erste Zeile"));
    const std::string before = fileText(path);  // noch nicht geschlossen
    CHECK(before.find("  Erste Zeile\r\n") != std::string::npos);
    CHECK(log.line("Zweite Zeile"));
    log.close();
    CHECK(!log.isOpen());
    CHECK(!log.line("nach close"));
    const std::string text = fileText(path);
    CHECK(text.find("Zweite Zeile\r\n") != std::string::npos);
    CHECK(text.find("nach close") == std::string::npos);
    CHECK_EQ(text.size(), size_t(2 * 21 + 11 + 12 + 2 * 2));  // 2× "YYYY-MM-DD HH:MM:SS  " + Texte + CRLF
}

TEST(audit_log_handles_utf8_path) {
    const std::string path = tmpPath("protokoll_jürgen.log");
    AuditLog log;
    CHECK(log.open(path));
    CHECK(log.line("Größe ok"));
    log.close();
    CHECK(fileText(path).find("Größe ok") != std::string::npos);
}

TEST(audit_log_open_failure_is_reported) {
    AuditLog log;
    CHECK(!log.open(tmpPath("gibt/es/nicht/x.log")));
    CHECK(!log.isOpen());
    CHECK(!log.line("x"));
}
```

Hinweis zu `readFile` mit UTF-8-Pfad: `readFile` nutzt `std::ifstream` mit schmalem Pfad. Unter Windows (MinGW) würde der Umlaut im ANSI-Codepage interpretiert. Deshalb liest `audit_log_handles_utf8_path` über eine eigene Hilfsfunktion, siehe Step 3.

- [ ] **Step 2: Tests laufen lassen, Fehlschlag prüfen**

Run: `make test-unit`
Expected: Kompilierfehler `audit_log.h: No such file or directory`

- [ ] **Step 3: UTF-8-Lesehilfe für Tests** – in `tests/test_audit_log.cpp` die Funktion `fileText` ersetzen durch eine Variante, die unter Windows den Pfad breit öffnet:

```cpp
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#endif

std::string fileText(const std::string& path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    std::FILE* f = _wfopen(w.c_str(), L"rb");
    if (!f) throw TestFailure("fileText fehlgeschlagen: " + path);
    std::string s;
    char buf[4096];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, got);
    std::fclose(f);
    return s;
#else
    const auto bytes = readFile(path);
    return std::string(bytes.begin(), bytes.end());
#endif
}
```

(Die `#ifdef _WIN32`-Includes stehen am Dateianfang nach den bestehenden Includes; `fileText` bleibt im anonymen Namespace.)

- [ ] **Step 4: `src/audit_log.h` anlegen**

```cpp
#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

#include "drive.h"
#include "wiper.h"

namespace dw {

// "2026-10-06 14:32:05  text\r\n"
std::string formatLogLine(const std::tm& t, const std::string& text);
// 15728640000 -> "15.728.640.000"
std::string groupDigits(uint64_t v);
// "Disk 3 – Modell – SN … – 15.728.640.000 Bytes – USB"
std::string driveLogText(const DriveInfo& d);
// Vorschlag für "Speichern unter"; unzulässige Zeichen und Leerzeichen werden zu '_'.
std::string suggestLogName(const std::tm& t, const DriveInfo& d);

std::string eventText(const Event& e);
// Erwartet r.resume.
std::string interruptionText(const Result& r, int totalPasses);
std::string resumedText(const ResumePoint& p, int totalPasses);
std::string contentCheckText(ContentCheck c);

std::tm localNow();

// Protokolldatei: jede Zeile mit lokalem Zeitstempel, sofort auf den Datenträger geschrieben.
class AuditLog {
public:
    AuditLog() = default;
    ~AuditLog() { close(); }
    AuditLog(const AuditLog&) = delete;
    AuditLog& operator=(const AuditLog&) = delete;

    bool open(const std::string& utf8Path);  // legt neu an bzw. überschreibt
    bool isOpen() const;
    const std::string& path() const { return path_; }
    bool line(const std::string& text);  // thread-sicher; false wenn nicht offen oder Schreibfehler
    void close();

private:
    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::string path_;
};

}  // namespace dw
```

- [ ] **Step 5: `src/audit_log.cpp` anlegen**

```cpp
#include "audit_log.h"

#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace dw {
namespace {

std::FILE* openUtf8(const std::string& path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0) return nullptr;
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    return _wfopen(w.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

bool syncToDisk(std::FILE* f) {
    if (std::fflush(f) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}

const char* phaseName(Phase p) { return p == Phase::Write ? "Schreiben" : "Prüfen"; }

std::string passLabel(int pass, int total) { return "Durchgang " + std::to_string(pass) + "/" + std::to_string(total); }

}  // namespace

std::string formatLogLine(const std::tm& t, const std::string& text) {
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d  ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec);
    return stamp + text + "\r\n";
}

std::string groupDigits(uint64_t v) {
    std::string digits = std::to_string(v);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<size_t>(i), ".");
    return digits;
}

std::string driveLogText(const DriveInfo& d) {
    return "Disk " + std::to_string(d.number) + " – " + (d.model.empty() ? "Unbekannt" : d.model) + " – SN " +
           (d.serial.empty() ? "unbekannt" : d.serial) + " – " + groupDigits(d.size) + " Bytes – " + driveKind(d);
}

std::string suggestLogName(const std::tm& t, const DriveInfo& d) {
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
                  t.tm_min, t.tm_sec);
    std::string model = d.model.empty() ? "Unbekannt" : d.model;
    for (char& c : model) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
            c = '_';
    }
    std::string name = std::string("diskwipe_") + stamp + "_" + model;
    if (!d.serial.empty()) name += "_SN" + (d.serial.size() > 4 ? d.serial.substr(d.serial.size() - 4) : d.serial);
    return name + ".log";
}

std::string eventText(const Event& e) {
    const std::string pass = passLabel(e.pass, e.totalPasses);
    if (e.kind == EventKind::PhaseStarted)
        return "Start " + pass + " (" + (e.pattern == PatternKind::Random ? "Zufall" : "Nullen") + "), " + phaseName(e.phase) +
               " ab Offset " + groupDigits(e.offset);
    std::string text = "Ende " + pass + ", " + phaseName(e.phase);
    if (e.phase == Phase::Verify) text += ": " + std::to_string(e.mismatches) + " Abweichungen";
    return text;
}

std::string interruptionText(const Result& r, int totalPasses) {
    const ResumePoint p = r.resume ? *r.resume : ResumePoint{};
    return "Unterbrechung in " + passLabel(p.pass, totalPasses) + " (" + phaseName(p.phase) + "): " + r.message + "; weiter ab " +
           (p.phase == Phase::Write ? "Checkpoint " : "Offset ") + groupDigits(p.offset);
}

std::string resumedText(const ResumePoint& p, int totalPasses) {
    return "Fortgesetzt in " + passLabel(p.pass, totalPasses) + ", " + phaseName(p.phase) + " ab Offset " + groupDigits(p.offset);
}

std::string contentCheckText(ContentCheck c) {
    switch (c) {
    case ContentCheck::Strong:
        return "Inhaltsprüfung: passt (Zufallsmuster des Vorgangs gefunden)";
    case ContentCheck::Weak:
        return "Inhaltsprüfung: keine eindeutige Zuordnung möglich – Bestätigung erforderlich";
    case ContentCheck::Mismatch:
        return "Inhaltsprüfung: Inhalt passt nicht zum unterbrochenen Vorgang";
    case ContentCheck::ReadError:
        break;
    }
    return "Inhaltsprüfung: Lesefehler";
}

std::tm localNow() {
    std::tm t{};
#ifdef _WIN32
    SYSTEMTIME s;
    GetLocalTime(&s);
    t.tm_year = s.wYear - 1900;
    t.tm_mon = s.wMonth - 1;
    t.tm_mday = s.wDay;
    t.tm_hour = s.wHour;
    t.tm_min = s.wMinute;
    t.tm_sec = s.wSecond;
#else
    const std::time_t now = std::time(nullptr);
    localtime_r(&now, &t);
#endif
    return t;
}

bool AuditLog::open(const std::string& utf8Path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) std::fclose(file_);
    file_ = openUtf8(utf8Path);
    path_ = file_ ? utf8Path : std::string();
    return file_ != nullptr;
}

bool AuditLog::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_ != nullptr;
}

bool AuditLog::line(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return false;
    const std::string l = formatLogLine(localNow(), text);
    return std::fwrite(l.data(), 1, l.size(), file_) == l.size() && syncToDisk(file_);
}

void AuditLog::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

}  // namespace dw
```

Hinweis: `close()` lässt `path_` stehen, damit die GUI den Pfad nach dem Schließen noch kennt.

- [ ] **Step 6: Tests laufen lassen (Linux und Windows)**

Run: `make test-unit && make test-win`
Expected: alle Tests PASS in beiden Läufen (insbesondere `audit_log_handles_utf8_path` unter Windows)

- [ ] **Step 7: Commit**

```bash
git add src/audit_log.h src/audit_log.cpp tests/test_audit_log.cpp
git commit -m "feat: audit log file with timestamped, synced lines"
```

---

### Task 6: GUI – Protokoll pro Lauf und Speichern-Dialog

**Files:**
- Modify: `src/gui_win.cpp`, `Makefile:68-70` (Link-Zeile von `diskwipe.exe`)

**Interfaces:**
- Consumes: `AuditLog`, `localNow`, `driveLogText`, `suggestLogName`, `eventText` (Task 5); `makePlan`, `secureRandomSeed`, `RunOptions`, `runPasses(dev, plan, opts, …)`, `runVerifyZero(…, events)` (Task 2).
- Produces (in `gui_win.cpp`, anonymer Namespace; Task 7 baut darauf auf):
  - `App`-Felder `AuditLog audit; std::string logName; std::vector<PassSpec> plan; DriveInfo runDrive; double startUnits = 0;`
  - `void startAudit(bool wipe, const DriveInfo& drive, int randomPasses);`
  - `void finishAudit(const std::string& outcome);` – schreibt `Ergebnis: <outcome>`, schließt, zeigt "Speichern unter".
  - `void launchWorker(const DriveInfo& drive, std::shared_ptr<BlockDevice> dev, bool wipe, const ResumePoint& start, double startUnits);` – `dev == nullptr` → Worker öffnet selbst; Plan kommt aus `g.plan`.

Keine automatischen Tests (Win32-GUI); Prüfung über `make windows` und manuelle Tests in Task 8.

- [ ] **Step 1: Makefile** – in der Link-Zeile von `$(BUILD)/diskwipe.exe` `-lcomdlg32 -lshell32` ergänzen:

```make
	  $(CORE_SRC) $(WIN_LIB_SRC) src/gui_win.cpp $(BUILD)/resource.o -lbcrypt -lcomctl32 -lole32 -luuid -luxtheme -lcomdlg32 -lshell32
```

- [ ] **Step 2: Includes und App-Felder** in `src/gui_win.cpp`:
  - nach `#include <commctrl.h>`: `#include <commdlg.h>`; nach `#include <shobjidl.h>`: `#include <shlobj.h>`
  - nach `#include "device_win.h"`: `#include "audit_log.h"`
  - in `struct App` nach `HWND result = nullptr, log = nullptr;` nichts ändern; am Ende von `struct App` ergänzen:

```cpp
    AuditLog audit;                 // Protokoll des laufenden bzw. unterbrochenen Vorgangs
    std::string logName;            // Vorschlag für "Speichern unter"
    std::vector<PassSpec> plan;     // Plan des laufenden Löschvorgangs (mit Seeds, nie protokolliert)
    DriveInfo runDrive;             // Laufwerk des laufenden Vorgangs
    double startUnits = 0;          // Fortschritt beim Start (für MB/s nach dem Fortsetzen)
```

- [ ] **Step 3: Protokoll-Hilfsfunktionen** – nach `setResult(...)` einfügen:

```cpp
void startAudit(bool wipe, const DriveInfo& drive, int randomPasses) {
    static int counter = 0;
    g.audit.close();
    const std::tm t = localNow();
    wchar_t dir[MAX_PATH + 1] = {};
    const DWORD len = GetTempPathW(MAX_PATH + 1, dir);
    wchar_t name[64];
    swprintf(name, 64, L"diskwipe-%04d%02d%02d-%02d%02d%02d-%d.log", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
             t.tm_min, t.tm_sec, ++counter);
    const std::wstring path = (len > 0 && len <= MAX_PATH ? std::wstring(dir, len) : std::wstring()) + name;
    g.logName = suggestLogName(t, drive);
    if (!g.audit.open(toUtf8(path))) {
        appendLog(L"Warnung: Protokolldatei kann nicht angelegt werden: " + path);
        return;
    }
    g.audit.line("diskwipe " DW_VERSION_STR " – Protokoll");
    g.audit.line(wipe ? "Vorgang: Löschen, " + std::to_string(randomPasses) + "× Zufall + 1× Nullen"
                      : std::string("Vorgang: Prüfen (alle Bytes = 0x00)"));
    g.audit.line("Laufwerk: " + driveLogText(drive));
}

// Schließt das Protokoll mit dem Ergebnis ab und bietet "Speichern unter" an.
void finishAudit(const std::string& outcome) {
    if (!g.audit.isOpen()) return;
    g.audit.line("Ergebnis: " + outcome);
    g.audit.close();
    const std::wstring temp = toWide(g.audit.path());

    std::wstring docs;
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &known)) && known) docs = known;
    CoTaskMemFree(known);
    wchar_t file[MAX_PATH] = {};
    const std::wstring suggestion = toWide(g.logName);
    wcsncpy(file, suggestion.c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = L"Protokoll (*.log)\0*.log\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = docs.empty() ? nullptr : docs.c_str();
    ofn.lpstrTitle = L"Protokoll speichern";
    ofn.lpstrDefExt = L"log";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    BOOL chosen;
    {
        ModalGuard guard;
        chosen = GetSaveFileNameW(&ofn);
    }
    if (chosen && CopyFileW(temp.c_str(), file, FALSE)) {
        DeleteFileW(temp.c_str());
        appendLog(L"Protokoll gespeichert: " + std::wstring(file));
        return;
    }
    if (chosen) {
        const std::wstring err = toWide(winErrorText(GetLastError()));
        ModalGuard guard;
        MessageBoxW(g.wnd, (L"Protokoll konnte nicht gespeichert werden: " + err).c_str(), L"diskwipe", MB_ICONERROR);
    }
    appendLog(L"Protokoll liegt unter: " + temp);
}
```

- [ ] **Step 4: `onProgress`** – die Geschwindigkeitszeile ersetzen, damit ein fortgesetzter Lauf keine überhöhte MB/s anzeigt:

```cpp
    const double speed = elapsed > 0 ? (unitsDone - g.startUnits) / elapsed : 0;
```

- [ ] **Step 5: `launchWorker` einführen** – den Block in `startOperation` ab `if (g.worker.joinable()) g.worker.join();` bis zum Ende der Funktion ersetzen. Neue Funktion vor `startOperation`:

```cpp
// Startet den Worker. dev: bereits geöffnetes Gerät (Fortsetzen) oder nullptr (Worker öffnet selbst).
// Beim Löschen kommt der Plan aus g.plan.
void launchWorker(const DriveInfo& drive, std::shared_ptr<BlockDevice> dev, bool wipe, const ResumePoint& start,
                  double startUnits) {
    if (g.worker.joinable()) g.worker.join();
    g.wipeMode = wipe;
    g.runDrive = drive;
    g.cancel = false;
    g.started = Clock::now();
    g.lastPost = Clock::time_point{};
    g.startUnits = startUnits;
    setTaskbarProgress(TBPF_NORMAL, 0);
    const double unitsTotal = double(wipe ? g.plan.size() * 2 : 1) * double(drive.size);
    setOverallProgress(unitsTotal > 0 ? static_cast<int>(startUnits * 1000.0 / unitsTotal) : 0);
    setResult(L"", RGB(0, 0, 0));
    SetWindowTextW(g.status, L"Laufwerk wird geöffnet …");
    setRunning(true);

    const std::vector<PassSpec> plan = g.plan;
    g.worker = std::thread([drive, dev, wipe, plan, start]() mutable {
        std::unique_ptr<Result> result;
        try {
            result.reset(new Result());
            std::string err;
            std::shared_ptr<BlockDevice> device = std::move(dev);
            if (!device) device = WinPhysicalDevice::open(drive, err);
            if (!device) {
                result->status = Status::IoError;
                result->message = err;
            } else {
                const ProgressFn report = [](const Progress& p) {
                    {
                        std::lock_guard<std::mutex> lock(g.progressMutex);
                        g.lastProgress = p;
                    }
                    const Clock::time_point now = Clock::now();
                    if (p.done == p.total || now - g.lastPost >= std::chrono::milliseconds(100)) {
                        g.lastPost = now;
                        PostMessageW(g.wnd, WM_APP_PROGRESS, 0, 0);
                    }
                };
                const EventFn events = [](const Event& e) { g.audit.line(eventText(e)); };
                RunOptions opts;
                opts.start = start;
                opts.events = events;
                *result = wipe ? runPasses(*device, plan, opts, report, g.cancel)
                               : runVerifyZero(*device, report, g.cancel, events);
            }
            device.reset();  // Volumes freigeben, bevor die GUI die Liste neu aufbaut
        } catch (const std::exception& e) {
            if (!result) result.reset(new Result());
            result->status = Status::IoError;
            result->message = e.what();
        } catch (...) {
            if (!result) result.reset(new Result());
            result->status = Status::IoError;
            result->message = "Unbekannter Fehler";
        }
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (PostMessageW(g.wnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(result.get()))) {
                result.release();  // Besitz geht an onDone über
                break;
            }
            Sleep(50);
        }
        // Schlägt das Posten dauerhaft fehl, räumt der unique_ptr das Ergebnis auf.
    });
}
```

Hinweis: `WinPhysicalDevice::open` liefert `std::unique_ptr<WinPhysicalDevice>`; die Zuweisung an `std::shared_ptr<BlockDevice>` ist implizit erlaubt.

Das Ende von `startOperation` (ab `if (g.worker.joinable())`) wird zu:

```cpp
    if (wipe) {
        try {
            g.plan = makePlan(randomPasses, secureRandomSeed);
        } catch (const std::exception& e) {
            const std::wstring text = L"Fehler: Zufallsquelle nicht verfügbar: " + toWide(e.what());
            setResult(text, RGB(190, 0, 0));
            appendLog(text);
            return;
        }
    } else {
        g.plan.clear();
    }
    appendLog((wipe ? L"Löschen gestartet: " : L"Prüfung gestartet: ") + toWide(driveText) +
              (wipe ? L" (" + std::to_wstring(randomPasses) + L"× Zufall + 1× Nullen)" : L""));
    startAudit(wipe, drive, randomPasses);
    launchWorker(drive, nullptr, wipe, ResumePoint{}, 0);
}
```

(Die frühere Initialisierung `g.wipeMode`, `g.cancel`, `g.started`, `g.lastPost`, Taskbar, Fortschritt, Ergebnis, Status und `setRunning(true)` sowie das Lambda in `startOperation` entfallen – sie stecken jetzt in `launchWorker`.)

- [ ] **Step 6: `onDone` – Protokoll abschließen.** Im `switch` jeweils das Ergebnis in eine Variable `outcome` übernehmen und nach dem `switch` abschließen. `onDone` vom Anfang bis zum Ende des `switch` ersetzen durch:

```cpp
void onDone(Result* raw) {
    std::unique_ptr<Result> r(raw);
    if (g.worker.joinable()) g.worker.join();
    setRunning(false);
    SetWindowTextW(g.status, L"Bereit");

    std::wstring outcome;
    switch (r->status) {
    case Status::Success: {
        setOverallProgress(1000);
        setTaskbarProgress(TBPF_NOPROGRESS);
        outcome = L"Erfolg: alle " + std::to_wstring(r->bytesTotal) + L" Bytes = 0x00";
        if (g.wipeMode) outcome += L" (" + std::to_wstring(r->passesCompleted) + L" Durchgänge geschrieben und verifiziert)";
        setResult(outcome, RGB(0, 128, 0));
        appendLog(outcome);
        if (g.wipeMode && !g.closing) {
            ModalGuard guard;
            MessageBoxW(g.wnd,
                        L"Der Datenträger wurde vollständig überschrieben und verifiziert.\n\n"
                        L"Hinweis: Reservebereiche des Flash-Controllers sind per Software nicht erreichbar. "
                        L"Für maximale Sicherheit den Stick zusätzlich physisch zerstören.",
                        L"diskwipe", MB_ICONINFORMATION);
        }
        break;
    }
    case Status::Cancelled: {
        outcome = g.wipeMode ? L"Abgebrochen – Datenträger unvollständig gelöscht" : L"Prüfung abgebrochen";
        setTaskbarProgress(TBPF_PAUSED);
        setResult(outcome, RGB(200, 110, 0));
        appendLog(outcome);
        break;
    }
    case Status::IoError: {
        outcome = L"Fehler: " + toWide(r->message);
        setTaskbarProgress(TBPF_ERROR);
        setResult(outcome, RGB(190, 0, 0));
        appendLog(outcome);
        break;
    }
    case Status::VerifyMismatch: {
        outcome = L"Prüfung fehlgeschlagen";
        setTaskbarProgress(TBPF_ERROR);
        if (g.wipeMode) outcome += L" in Durchgang " + std::to_wstring(r->failedPass);
        outcome += L": " + std::to_wstring(r->mismatchCount) + L" abweichende Bytes, erste bei Offset " +
                   std::to_wstring(r->firstMismatch);
        setResult(outcome, RGB(190, 0, 0));
        appendLog(outcome);
        const std::wstring data = L"Daten ab Offset " + std::to_wstring(r->firstMismatch) + L": " + hexExcerpt(r->excerpt);
        appendLog(data);
        g.audit.line(toUtf8(data));
        break;
    }
    }
    if (g.closing) g.audit.line("Programm beendet");
    finishAudit(toUtf8(outcome));
```

(Der Rest von `onDone` ab `if (g.closing) { DestroyWindow… }` bleibt unverändert.)

- [ ] **Step 7: Bauen**

Run: `make windows && make test-unit`
Expected: `build/diskwipe.exe` entsteht ohne Fehler und ohne neue Warnungen; alle Unit-Tests PASS.

- [ ] **Step 8: Commit**

```bash
git add Makefile src/gui_win.cpp
git commit -m "feat: write an audit log per run and offer it via save dialog"
```

---

### Task 7: GUI – Fortsetzen nach Wiedereinstecken

**Files:**
- Modify: `src/gui_win.cpp`

**Interfaces:**
- Consumes: Task 6 (`launchWorker`, `startAudit`, `finishAudit`, `g.audit`, `g.plan`, `g.runDrive`); `findByIdentity` (Task 4); `checkResumeContent`, `ContentCheck` (Task 3); `interruptionText`, `resumedText`, `contentCheckText`, `driveLogText` (Task 5).
- Produces: Button `IDC_RESUME`; `struct PendingResume`; Funktionen `enterPending`, `updatePending`, `discardPending`, `confirmDiscard`, `resumeOperation`.

Keine automatischen Tests (Win32-GUI); Prüfung über `make windows` und die manuellen Tests aus Task 8.

- [ ] **Step 1: Control-ID, Zustand, Button**
  - Enum: `IDC_LOG,` → `IDC_LOG, IDC_RESUME,`
  - vor `struct App` einfügen:

```cpp
// Unterbrochener Löschvorgang, der nach dem Wiedereinstecken fortgesetzt werden kann.
struct PendingResume {
    bool active = false;
    bool available = false;      // genau ein passendes Laufwerk in der Liste
    DriveInfo identity;          // Laufwerk beim Start (Nummer ändert sich beim Umstecken)
    std::vector<PassSpec> plan;
    ResumePoint point;
    uint64_t total = 0;          // Größe in Bytes
};
```

  - in `struct App`: `HWND wipeBtn = nullptr, verifyBtn = nullptr, cancelBtn = nullptr, …` um `resumeBtn = nullptr` ergänzen und am Ende `PendingResume pending;` ergänzen.
  - `createControls`: nach dem Abbrechen-Button

```cpp
    g.resumeBtn = makeChild(WC_BUTTONW, L"Fortsetzen", BS_PUSHBUTTON | WS_TABSTOP, 336, 106, 100, 30, IDC_RESUME);
```

  - `setRunning`: die Zeile `EnableWindow(g.cancelBtn, running);` ersetzen durch

```cpp
    EnableWindow(g.cancelBtn, running || g.pending.active);
    EnableWindow(g.resumeBtn, !running && g.pending.active && g.pending.available);
```

- [ ] **Step 2: Status- und Zustandsfunktionen** – direkt nach `flushPendingRefresh()` einfügen (dort sind `setRunning`, `setResult`, `finishAudit` und `flushPendingRefresh` bereits definiert):

```cpp
std::wstring pendingStatus() {
    const ResumePoint& p = g.pending.point;
    const uint64_t pct = g.pending.total ? p.offset * 100 / g.pending.total : 0;
    return L"Unterbrochen – Durchgang " + std::to_wstring(p.pass) + L"/" + std::to_wstring(g.pending.plan.size()) + L", " +
           (p.phase == Phase::Write ? L"Schreiben" : L"Prüfen") + L" bei " + std::to_wstring(pct) + L" %";
}

// Nach jeder Aktualisierung der Liste: passendes Laufwerk für den unterbrochenen Vorgang suchen.
void updatePending() {
    if (!g.pending.active || g.running) return;
    const std::vector<size_t> hits = findByIdentity(g.drives, g.pending.identity);
    const bool was = g.pending.available;
    g.pending.available = hits.size() == 1;
    std::wstring status = pendingStatus();
    if (hits.size() == 1) {
        if (!was) {  // nur beim Wiedererkennen auswählen, danach darf der Nutzer frei wählen
            SendMessageW(g.drive, CB_SETCURSEL, static_cast<WPARAM>(hits[0]), 0);
            appendLog(L"Laufwerk des unterbrochenen Vorgangs erkannt: " + toWide(g.driveTexts[hits[0]]));
        }
        status += L" – Laufwerk erkannt, „Fortsetzen“ klicken";
    } else if (hits.size() > 1) {
        status += L" – mehrere passende Laufwerke, das andere abziehen";
    } else {
        status += L" – Laufwerk wieder einstecken";
    }
    SetWindowTextW(g.status, status.c_str());
}

void enterPending(const Result& r) {
    g.pending.active = true;
    g.pending.available = false;
    g.pending.identity = g.runDrive;
    g.pending.plan = g.plan;
    g.pending.point = *r.resume;
    g.pending.total = r.bytesTotal;
    g.audit.line(interruptionText(r, static_cast<int>(g.plan.size())));
    setTaskbarProgress(TBPF_PAUSED, g.taskbarDone);
    const std::wstring text = L"Unterbrochen: " + toWide(r.message);
    setResult(text, RGB(200, 110, 0));
    appendLog(text);
    appendLog(L"Laufwerk wieder einstecken und „Fortsetzen“ klicken, um weiterzumachen.");
    SetWindowTextW(g.status, (pendingStatus() + L" – Laufwerk wieder einstecken").c_str());
}

void discardPending() {
    if (!g.pending.active) return;
    g.pending = PendingResume{};
    g.audit.line("Unterbrochener Vorgang verworfen");
    appendLog(L"Unterbrochener Vorgang verworfen.");
    setTaskbarProgress(TBPF_NOPROGRESS);
    setResult(L"Verworfen – Datenträger unvollständig gelöscht", RGB(200, 110, 0));
    SetWindowTextW(g.status, L"Bereit");
    setRunning(false);
    finishAudit("Abgebrochen – Datenträger unvollständig gelöscht");
}

// Rückfrage; bei "Ja" wird der unterbrochene Vorgang verworfen und das Protokoll abgeschlossen.
bool confirmDiscard() {
    int answer;
    {
        ModalGuard guard;
        answer = MessageBoxW(g.wnd,
                             L"Ein unterbrochener Löschvorgang wartet auf Fortsetzung.\n"
                             L"Verwerfen? Der Datenträger bleibt dann unvollständig gelöscht.",
                             L"diskwipe", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    }
    if (answer != IDYES) {
        flushPendingRefresh();
        return false;
    }
    discardPending();
    return true;
}
```

Hinweis zur Reihenfolge: `refreshDrives` steht vor diesen Funktionen und ruft `updatePending` auf – deshalb direkt vor `refreshDrives` eine Vorwärtsdeklaration `void updatePending();` einfügen.

- [ ] **Step 3: `refreshDrives` meldet an `updatePending`** – an beiden Stellen, an denen `refreshDrives` `setRunning(false);` aufruft (früher Rücksprung bei `identical` und am Ende), davor `updatePending();` einfügen:

```cpp
            g.drives = fresh;
            updatePending();
            setRunning(false);
            return;
```

```cpp
    updatePending();
    setRunning(false);
    if (!automatic)
```

- [ ] **Step 4: `startOperation` – offenen Stand vorher verwerfen.** Als erste Zeile von `startOperation`:

```cpp
    if (g.pending.active && !confirmDiscard()) return;
```

- [ ] **Step 5: `onDone` – Unterbrechung erkennen.** Direkt nach `SetWindowTextW(g.status, L"Bereit");` einfügen und den bisherigen `switch` samt `finishAudit`-Aufruf aus Task 6 in den `else`-Zweig verschieben:

```cpp
    const bool resumable = g.wipeMode && r->status == Status::IoError && r->resume && !g.closing;
    if (resumable && !g.runDrive.serial.empty()) {
        enterPending(*r);
    } else {
        if (resumable) {
            appendLog(L"Fortsetzen nicht möglich: Laufwerk meldet keine Seriennummer.");
            g.audit.line("Fortsetzen nicht möglich: Laufwerk meldet keine Seriennummer");
        }
        g.pending = PendingResume{};
        std::wstring outcome;
        switch (r->status) {
            // … unverändert aus Task 6 …
        }
        if (g.closing) g.audit.line("Programm beendet");
        finishAudit(toUtf8(outcome));
    }
    setRunning(false);  // Abbrechen/Fortsetzen passend zum neuen Stand
```

(Der Rest ab `if (g.closing) { DestroyWindow… }` bleibt; der anschließende `refreshDrives`-Aufruf ruft `updatePending` auf.)

- [ ] **Step 6: `resumeOperation`** – nach `startOperation` einfügen:

```cpp
void resumeOperation() {
    if (!g.pending.active || g.running) return;
    const std::vector<DriveInfo> all = listDrives();
    const std::vector<size_t> hits = findByIdentity(all, g.pending.identity);
    if (hits.size() != 1) {
        appendLog(hits.empty() ? L"Fortsetzen: Laufwerk nicht gefunden."
                               : L"Fortsetzen: mehrere passende Laufwerke – das andere abziehen.");
        refreshDrives(true);
        return;
    }
    const DriveInfo drive = all[hits[0]];
    g.audit.line("Laufwerk wieder erkannt: " + driveLogText(drive));

    SetWindowTextW(g.status, L"Laufwerk wird geöffnet und geprüft …");
    const HCURSOR oldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::string err;
    std::unique_ptr<WinPhysicalDevice> dev = WinPhysicalDevice::open(drive, err);
    ContentCheck check = ContentCheck::ReadError;
    if (dev) check = checkResumeContent(*dev, g.pending.plan, g.pending.point, err);
    SetCursor(oldCursor);

    auto refuse = [&](const std::wstring& text, const std::string& logText) {
        setResult(text, RGB(190, 0, 0));
        appendLog(text);
        g.audit.line(logText);
        dev.reset();
        updatePending();
    };
    if (!dev) return refuse(L"Fortsetzen nicht möglich: " + toWide(err), "Fortsetzen nicht möglich: " + err);
    g.audit.line(contentCheckText(check));
    if (check == ContentCheck::ReadError)
        return refuse(L"Fortsetzen nicht möglich: " + toWide(err), "Fortsetzen nicht möglich: " + err);
    if (check == ContentCheck::Mismatch)
        return refuse(L"Inhalt passt nicht zum unterbrochenen Vorgang – falscher Stick?",
                      "Fortsetzen abgelehnt: Inhalt passt nicht");
    if (check == ContentCheck::Weak && g.pending.point.phase == Phase::Write) {
        const ConfirmInfo info{describeDrive(drive), drive.volumes};
        INT_PTR confirmed;
        {
            ModalGuard guard;
            confirmed = DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_CONFIRM), g.wnd, confirmProc,
                                        reinterpret_cast<LPARAM>(&info));
        }
        if (confirmed != IDOK) {
            dev.reset();
            appendLog(L"Fortsetzen nicht bestätigt.");
            g.audit.line("Fortsetzen nicht bestätigt");
            updatePending();
            flushPendingRefresh();
            return;
        }
    }

    const ResumePoint start = g.pending.point;
    g.audit.line(resumedText(start, static_cast<int>(g.pending.plan.size())));
    appendLog(L"Fortgesetzt: " + toWide(describeDrive(drive)));
    g.plan = g.pending.plan;
    const double startUnits = double((start.pass - 1) * 2 + (start.phase == Phase::Verify ? 1 : 0)) * double(g.pending.total) +
                              double(start.offset);
    launchWorker(drive, std::shared_ptr<BlockDevice>(std::move(dev)), true, start, startUnits);
}
```

Hinweis: `g.pending` bleibt während des fortgesetzten Laufs aktiv (Button gesperrt, weil `running`); `onDone` setzt den Stand bei erneuter Unterbrechung neu bzw. löscht ihn bei jedem anderen Ende.

- [ ] **Step 7: Befehle und Schließen**
  - `WM_COMMAND`: `case IDC_CANCEL:` ersetzen durch

```cpp
        case IDC_CANCEL:
            if (g.running) {
                g.cancel = true;
                EnableWindow(g.cancelBtn, FALSE);
                appendLog(L"Abbruch angefordert …");
            } else if (g.pending.active) {
                confirmDiscard();
            }
            return 0;
        case IDC_RESUME:
            resumeOperation();
            return 0;
```

  - `WM_CLOSE`: direkt vor dem abschließenden `DestroyWindow(wnd); return 0;` (nach dem `if (g.running) {…}`-Block) einfügen:

```cpp
        if (g.pending.active) {
            int answer;
            {
                ModalGuard guard;
                answer = MessageBoxW(wnd,
                                     L"Ein unterbrochener Löschvorgang wartet auf Fortsetzung und geht beim Beenden verloren.\n"
                                     L"Trotzdem beenden?",
                                     L"diskwipe", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            }
            if (answer != IDYES) {
                flushPendingRefresh();
                return 0;
            }
            g.closing = true;
            g.audit.line("Programm beendet");
            discardPending();
        }
```

- [ ] **Step 8: Bauen**

Run: `make windows && make test-unit && make test-win`
Expected: `build/diskwipe.exe` ohne Fehler/neue Warnungen; alle Tests PASS.

- [ ] **Step 9: Commit**

```bash
git add src/gui_win.cpp
git commit -m "feat: resume an interrupted wipe after re-plugging the drive"
```

---

### Task 8: Dokumentation

**Files:**
- Modify: `docs/manual-test.md` (anhängen), `README.md`

- [ ] **Step 1: `docs/manual-test.md`** – am Ende anhängen:

```markdown
## Fortsetzen nach Abstecken und Protokoll
22. Löschen mit 3 Zufallsdurchgängen starten. In Durchgang 2 beim Schreiben den Stick abziehen → oranger Status „Unterbrochen: …", Statuszeile „Unterbrochen – Durchgang 2/4, Schreiben bei N % – Laufwerk wieder einstecken", Taskleiste gelb, „Fortsetzen" ausgegraut, „Abbrechen" aktiv. Kein Speichern-Dialog.
23. Stick an einem **anderen** USB-Port wieder einstecken → nach ca. einer halben Sekunde wird er ausgewählt, Log „Laufwerk des unterbrochenen Vorgangs erkannt: …", „Fortsetzen" aktiv. Klicken → Lauf geht ab ungefähr derselben Prozentzahl weiter (höchstens 64 MiB zurück), ohne LÖSCHEN-Abfrage. Ende grün, danach „Speichern unter" mit Vorschlag `diskwipe_<Datum>_<Modell>_SN<…>.log` im Ordner Dokumente.
24. Gespeichertes Protokoll im Editor öffnen: Kopf mit Version, Vorgang und Laufwerk (mit Seriennummer); je Phase Start/Ende mit Zeitstempel; „Unterbrechung in Durchgang 2/4 (Schreiben): …"; „Laufwerk wieder erkannt: Disk …"; „Inhaltsprüfung: passt …"; „Fortgesetzt in Durchgang 2/4 …"; letzte Zeile „Ergebnis: Erfolg …". Keine Seeds.
25. Wie 22, dann statt des Sticks einen **anderen** Stick gleichen Modells einstecken (falls vorhanden; sonst Schritt überspringen): Hat er eine andere Seriennummer, bleibt „Fortsetzen" grau. Hat er dieselbe Seriennummer (Billig-Klon), wird beim Klick auf „Fortsetzen" „Inhalt passt nicht zum unterbrochenen Vorgang – falscher Stick?" gemeldet; mit HxD prüfen, dass dieser Stick unverändert ist.
26. Wie 22, beide Sticks gleichzeitig stecken (nur bei gleicher Seriennummer relevant) → Statuszeile „mehrere passende Laufwerke, das andere abziehen".
27. Wie 22, dann „Abbrechen" → Rückfrage „Verwerfen?"; „Nein" → Stand bleibt; „Ja" → oranger Status „Verworfen – …", Speichern-Dialog, Protokoll endet mit „Ergebnis: Abgebrochen – Datenträger unvollständig gelöscht". Ebenso: bei offenem Stand „Löschen" oder „Prüfen" klicken → dieselbe Rückfrage; Fenster schließen → Rückfrage „Trotzdem beenden?".
28. „Prüfen" starten und Stick abziehen → roter Fehler, **kein** Fortsetzen-Stand, Speichern-Dialog erscheint.
29. Stick in der Prüfphase eines Löschdurchgangs abziehen, wieder einstecken, „Fortsetzen" → Prüfphase läuft ab dem unterbrochenen Block weiter, Ende grün.
30. Speichern-Dialog mit „Abbrechen" schließen → Log-Feld zeigt „Protokoll liegt unter: …\diskwipe-….log"; die Datei existiert in `%TEMP%`.
```

- [ ] **Step 2: `README.md`** – in der Feature-Liste nach dem Punkt zur automatischen Laufwerksliste ergänzen:

```markdown
- Wird der Stick während des Löschens abgezogen, kann der Vorgang nach dem Wiedereinstecken mit **Fortsetzen** an der unterbrochenen Stelle weiterlaufen (solange diskwipe geöffnet bleibt). Der Stick wird über Seriennummer und Inhalt wiedererkannt; ein anderer Stick wird nicht beschrieben
- Jeder Lauf schreibt ein Protokoll mit Zeitstempeln (Durchgänge, Unterbrechungen, Ergebnis), das am Ende per „Speichern unter" abgelegt wird
```

- [ ] **Step 3: Commit**

```bash
git add docs/manual-test.md README.md
git commit -m "docs: manual tests and README for resume and audit log"
```
