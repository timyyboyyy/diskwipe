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
    bool failed() const;                 // sticky: ein Schreib-/Sync-Fehler seit open()
    void close();

private:
    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::string path_;
    bool failed_ = false;
};

}  // namespace dw
