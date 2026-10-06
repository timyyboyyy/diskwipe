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
