#include <ctime>
#include <string>

#include "audit_log.h"
#include "test.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#endif

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
    p.mismatches = 7;
    p.firstMismatch = 1048576;
    r.resume = p;
    CHECK_EQ(interruptionText(r, 4),
             std::string("Unterbrechung in Durchgang 2/4 (Prüfen): Schreibfehler bei Offset 6012534784: "
                         "Das Gerät ist nicht bereit.; weiter ab Offset 5.972.688.896; "
                         "bisher 7 abweichende Bytes, erste bei Offset 1.048.576"));
}

TEST(audit_failed_flag) {
    AuditLog log;
    CHECK(!log.failed());
    CHECK(!log.line("closed"));  // nicht offen ist kein Schreibfehler
    CHECK(!log.failed());
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

TEST(format_seconds_and_rate) {
    CHECK_EQ(formatSeconds(0), std::string("0:00:00"));
    CHECK_EQ(formatSeconds(59.6), std::string("0:01:00"));
    CHECK_EQ(formatSeconds(3725), std::string("1:02:05"));
    CHECK_EQ(formatRate(20940000.0), std::string("20,9 MB/s"));
    CHECK_EQ(formatRate(0), std::string("0,0 MB/s"));
}

TEST(checkpoint_event_text) {
    CHECK_EQ(eventText(Event{EventKind::Checkpoint, 2, 4, Phase::Write, PatternKind::Random, 67108864, 0}),
             std::string("Checkpoint Durchgang 2/4: bis Offset 67.108.864 geschrieben und geflusht"));
}

TEST(run_logger_phase_summary) {
    RunLogger log;
    const Event start{EventKind::PhaseStarted, 1, 2, Phase::Write, PatternKind::Random, 0, 0};
    auto lines = log.onEvent(start, 10.0);
    CHECK_EQ(lines.size(), size_t(1));
    CHECK_EQ(lines[0], eventText(start));
    lines = log.onEvent(Event{EventKind::PhaseCompleted, 1, 2, Phase::Write, PatternKind::Random, 100000000, 0}, 15.0);
    CHECK_EQ(lines.size(), size_t(1));
    CHECK_EQ(lines[0], std::string("Ende Durchgang 1/2, Schreiben: 100.000.000 Bytes in 0:00:05, Ø 20,0 MB/s"));

    // Fortgesetzte Prüfphase: nur der Rest ab dem Start-Offset zählt.
    log.onEvent(Event{EventKind::PhaseStarted, 1, 2, Phase::Verify, PatternKind::Random, 40000000, 0}, 20.0);
    lines = log.onEvent(Event{EventKind::PhaseCompleted, 1, 2, Phase::Verify, PatternKind::Random, 100000000, 3}, 23.0);
    CHECK_EQ(lines[0], std::string("Ende Durchgang 1/2, Prüfen: 60.000.000 Bytes in 0:00:03, Ø 20,0 MB/s, 3 Abweichungen"));
}

TEST(run_logger_progress_every_5_percent_or_minute) {
    RunLogger log;
    log.onEvent(Event{EventKind::PhaseStarted, 1, 1, Phase::Write, PatternKind::Zero, 0, 0}, 0.0);
    const uint64_t total = 1000000000;
    CHECK(log.onProgress(Progress{1, 1, Phase::Write, 10000000, total}, 1.0).empty());  // 1 %
    CHECK_EQ(log.onProgress(Progress{1, 1, Phase::Write, 50000000, total}, 2.5),
             std::string("Fortschritt Durchgang 1/1, Schreiben: 5 % (Offset 50.000.000 von 1.000.000.000), "
                         "aktuell 20,0 MB/s, Ø 20,0 MB/s, Rest der Phase ca. 0:00:48"));
    CHECK(log.onProgress(Progress{1, 1, Phase::Write, 60000000, total}, 30.0).empty());   // 6 %, < 60 s
    CHECK(!log.onProgress(Progress{1, 1, Phase::Write, 70000000, total}, 63.0).empty());  // 60 s vergangen
    CHECK(log.onProgress(Progress{1, 1, Phase::Write, total, total}, 70.0).empty());      // Ende: PhaseCompleted
}

TEST(audit_log_counts_lines) {
    const std::string path = tmpPath("audit_lines.log");
    AuditLog log;
    CHECK_EQ(log.lines(), size_t(0));
    CHECK(log.open(path));
    CHECK(log.line("a"));
    CHECK(log.line("b"));
    CHECK_EQ(log.lines(), size_t(2));
    log.close();
    CHECK(!log.line("c"));
    CHECK_EQ(log.lines(), size_t(2));  // bleibt nach close für den Vergleich "gespeichert?"
    CHECK(log.open(path));
    CHECK_EQ(log.lines(), size_t(0));
    log.close();
}
