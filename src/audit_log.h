#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

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

// "1:02:05" (Stunden:Minuten:Sekunden, gerundet)
std::string formatSeconds(double seconds);
// Dezimal-MB: 20940000 -> "20,9 MB/s"
std::string formatRate(double bytesPerSecond);

std::string eventText(const Event& e);
// Erwartet r.resume.
std::string interruptionText(const Result& r, int totalPasses);
std::string resumedText(const ResumePoint& p, int totalPasses);
std::string contentCheckText(ContentCheck c);

std::tm localNow();

// Erzeugt die ausführlichen Protokollzeilen eines Laufs aus Ereignissen und Fortschritt.
// Zeiten in Sekunden auf einer beliebigen, monoton steigenden Uhr. Nicht thread-sicher:
// nur aus dem Thread benutzen, der Ereignisse und Fortschritt meldet.
class RunLogger {
public:
    // Phasenstart/Checkpoint: Ereignistext; Phasenende: Bytes, Dauer, Ø MB/s (Prüfen: Abweichungen).
    std::vector<std::string> onEvent(const Event& e, double now);
    // Fortschrittszeile bei jedem 5-%-Schritt der Phase, spätestens alle 60 s; sonst leer.
    std::string onProgress(const Progress& p, double now);

private:
    double phaseStart_ = 0, lastLineTime_ = 0;
    uint64_t startOffset_ = 0, lastLineDone_ = 0, lastStep_ = 0;
};

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
    size_t lines() const;                // erfolgreich geschriebene Zeilen seit open(), bleibt nach close()
    void close();

private:
    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::string path_;
    bool failed_ = false;
    size_t lines_ = 0;
};

}  // namespace dw
