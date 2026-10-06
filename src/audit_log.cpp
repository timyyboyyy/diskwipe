#include "audit_log.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
    std::string text = "Unterbrechung in " + passLabel(p.pass, totalPasses) + " (" + phaseName(p.phase) + "): " + r.message +
                       "; weiter ab " + (p.phase == Phase::Write ? "Checkpoint " : "Offset ") + groupDigits(p.offset);
    if (p.phase == Phase::Verify && p.mismatches > 0)
        text += "; bisher " + std::to_string(p.mismatches) + " abweichende Bytes, erste bei Offset " + groupDigits(p.firstMismatch);
    return text;
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
    failed_ = false;
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
    const bool ok = std::fwrite(l.data(), 1, l.size(), file_) == l.size() && syncToDisk(file_);
    if (!ok) failed_ = true;
    return ok;
}

bool AuditLog::failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

void AuditLog::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

}  // namespace dw
