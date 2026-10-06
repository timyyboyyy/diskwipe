#include "audit_log.h"

#include <algorithm>

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
    char stamp[64];  // Platz für beliebige int-Werte (sonst -Wformat-truncation)
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
    char stamp[64];  // Platz für beliebige int-Werte (sonst -Wformat-truncation)
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

std::string formatSeconds(double seconds) {
    const long long s = seconds > 0 ? static_cast<long long>(seconds + 0.5) : 0;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

std::string formatRate(double bytesPerSecond) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f MB/s", bytesPerSecond > 0 ? bytesPerSecond / 1e6 : 0.0);
    std::string s(buf);
    const size_t dot = s.find('.');
    if (dot != std::string::npos) s[dot] = ',';
    return s;
}

std::string eventText(const Event& e) {
    const std::string pass = passLabel(e.pass, e.totalPasses);
    if (e.kind == EventKind::Checkpoint)
        return "Checkpoint " + pass + ": bis Offset " + groupDigits(e.offset) + " geschrieben und geflusht";
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
    lines_ = 0;
    return file_ != nullptr;
}

std::vector<std::string> RunLogger::onEvent(const Event& e, double now) {
    if (e.kind == EventKind::PhaseStarted) {
        phaseStart_ = lastLineTime_ = now;
        startOffset_ = lastLineDone_ = e.offset;
        lastStep_ = 0;
        return {eventText(e)};
    }
    if (e.kind == EventKind::Checkpoint) return {eventText(e)};
    const uint64_t bytes = e.offset > startOffset_ ? e.offset - startOffset_ : 0;
    const double seconds = now - phaseStart_;
    std::string text = "Ende " + passLabel(e.pass, e.totalPasses) + ", " + phaseName(e.phase) + ": " + groupDigits(bytes) +
                       " Bytes in " + formatSeconds(seconds) + ", Ø " + formatRate(seconds > 0 ? bytes / seconds : 0);
    if (e.phase == Phase::Verify) text += ", " + std::to_string(e.mismatches) + " Abweichungen";
    return {text};
}

std::string RunLogger::onProgress(const Progress& p, double now) {
    if (p.total == 0 || p.done >= p.total) return {};
    const uint64_t step = p.done * 20 / p.total;  // 5-%-Schritte
    if (step <= lastStep_ && now - lastLineTime_ < 60.0) return {};
    const double sinceLast = now - lastLineTime_;
    const double sinceStart = now - phaseStart_;
    const double current = sinceLast > 0 ? (p.done - lastLineDone_) / sinceLast : 0;
    const double average = sinceStart > 0 ? (p.done - startOffset_) / sinceStart : 0;
    std::string text = "Fortschritt " + passLabel(p.pass, p.totalPasses) + ", " + phaseName(p.phase) + ": " +
                       std::to_string(p.done * 100 / p.total) + " % (Offset " + groupDigits(p.done) + " von " +
                       groupDigits(p.total) + "), aktuell " + formatRate(current) + ", Ø " + formatRate(average);
    if (average > 0) text += ", Rest der Phase ca. " + formatSeconds((p.total - p.done) / average);
    lastStep_ = std::max(lastStep_, step);
    lastLineTime_ = now;
    lastLineDone_ = p.done;
    return text;
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
    if (ok) ++lines_;
    else failed_ = true;
    return ok;
}

bool AuditLog::failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

size_t AuditLog::lines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lines_;
}

void AuditLog::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

}  // namespace dw
