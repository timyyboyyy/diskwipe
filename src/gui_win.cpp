// Minimalistische Win32-Oberfläche für diskwipe.
#ifndef _WIN32_WINNT
// Ziel Windows 10/11; MinGW deklariert sonst ITaskbarList3/ChangeWindowMessageFilterEx nicht.
#define _WIN32_WINNT 0x0A00
#endif
#include <algorithm>
#include <windows.h>
#include <commctrl.h>
#include <dbt.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <atomic>
#include <chrono>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "device_win.h"
#include "drive.h"
#include "enumerate_win.h"
#include "resource.h"
#include "util_win.h"
#include "version.h"
#include "wiper.h"

using namespace dw;
using Clock = std::chrono::steady_clock;

namespace {

enum : int {
    IDC_DRIVE = 101, IDC_REFRESH, IDC_INTERNAL, IDC_PASSES, IDC_SPIN,
    IDC_WIPE, IDC_VERIFY, IDC_CANCEL, IDC_PROGRESS, IDC_STATUS, IDC_RESULT, IDC_LOG,
};
constexpr UINT WM_APP_PROGRESS = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;
constexpr UINT_PTR IDT_REFRESH = 1;
constexpr UINT kRefreshDebounceMs = 500;
constexpr int kMaxRandomPasses = 10;
// GUID_DEVINTERFACE_DISK
const GUID kDiskInterfaceGuid = {0x53f56307, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};
const wchar_t* const kConfirmWord = L"LÖSCHEN";

struct ConfirmInfo {
    std::string text;                  // Combobox-Text des gewählten Eintrags
    std::vector<std::string> volumes;  // Volumes des Laufwerks
};

struct App {
    HINSTANCE inst = nullptr;
    HWND wnd = nullptr, drive = nullptr, refresh = nullptr, internal = nullptr, passes = nullptr, spin = nullptr;
    HWND wipeBtn = nullptr, verifyBtn = nullptr, cancelBtn = nullptr, progress = nullptr, status = nullptr;
    HWND result = nullptr, log = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    std::vector<DriveInfo> drives;
    std::vector<std::string> driveTexts;  // Beschreibung je Eintrag, identisch mit Combobox
    std::thread worker;
    std::atomic<bool> cancel{false};
    bool running = false;
    bool wipeMode = false;
    bool closing = false;
    int modal = 0;                // Tiefe offener modaler Dialoge: keine Listenänderung
    bool refreshPending = false;  // Geräteänderung während eines Laufs, wird danach nachgeholt
    HDEVNOTIFY devNotify = nullptr;
    std::mutex progressMutex;
    Progress lastProgress;
    Clock::time_point started;
    Clock::time_point lastPost;  // nur im Worker-Thread benutzt
    COLORREF resultColor = RGB(0, 0, 0);
    int overallPercent = -1;  // 0..100, -1 = noch kein Vorgang (kein Text im Balken)
    UINT taskbarCreatedMsg = 0;
    ITaskbarList3* taskbar = nullptr;  // erst nach "TaskbarButtonCreated" vorhanden
    TBPFLAG taskbarState = TBPF_NOPROGRESS;
    ULONGLONG taskbarDone = 0;
};
App g;

struct ModalGuard {
    ModalGuard() { ++g.modal; }
    ~ModalGuard() { --g.modal; }
    ModalGuard(const ModalGuard&) = delete;
    ModalGuard& operator=(const ModalGuard&) = delete;
};

int S(int v) { return MulDiv(v, g.dpi, 96); }

std::wstring germanDecimal(double v) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f", v);
    for (wchar_t* p = buf; *p; ++p)
        if (*p == L'.') *p = L',';
    return buf;
}

std::wstring formatDuration(double seconds) {
    const long s = static_cast<long>(seconds + 0.5);
    wchar_t buf[32];
    if (s >= 3600) swprintf(buf, 32, L"%ld:%02ld h", s / 3600, (s / 60) % 60);
    else swprintf(buf, 32, L"%ld:%02ld min", s / 60, s % 60);
    return buf;
}

std::wstring hexExcerpt(const std::vector<uint8_t>& bytes) {
    std::wstring s;
    wchar_t b[4];
    for (uint8_t x : bytes) {
        swprintf(b, 4, L"%02X ", x);
        s += b;
    }
    return s;
}

void appendLog(const std::wstring& text) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[16];
    swprintf(stamp, 16, L"[%02u:%02u:%02u] ", t.wHour, t.wMinute, t.wSecond);
    const std::wstring line = stamp + text + L"\r\n";
    const int len = GetWindowTextLengthW(g.log);
    SendMessageW(g.log, EM_SETSEL, len, len);
    SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

void setResult(const std::wstring& text, COLORREF color) {
    g.resultColor = color;
    SetWindowTextW(g.result, text.c_str());
    InvalidateRect(g.result, nullptr, TRUE);
}

void setRunning(bool running) {
    g.running = running;
    const bool haveDrive = SendMessageW(g.drive, CB_GETCURSEL, 0, 0) != CB_ERR;
    EnableWindow(g.drive, !running);
    EnableWindow(g.refresh, !running);
    EnableWindow(g.internal, !running);
    EnableWindow(g.passes, !running);
    EnableWindow(g.spin, !running);
    EnableWindow(g.wipeBtn, !running && haveDrive);
    EnableWindow(g.verifyBtn, !running && haveDrive);
    EnableWindow(g.cancelBtn, running);
}

bool sameDrive(const DriveInfo& a, const DriveInfo& b) {
    return a.number == b.number && a.serial == b.serial && a.size == b.size;
}

int findDrive(const std::vector<DriveInfo>& list, const DriveInfo& d) {
    for (size_t i = 0; i < list.size(); ++i)
        if (sameDrive(list[i], d)) return static_cast<int>(i);
    return -1;
}

// automatic = durch Geräteereignis ausgelöst: Hinzu-/Entfernt-Log statt "N Laufwerk(e) gefunden."
void refreshDrives(bool automatic = false) {
    const bool includeInternal = SendMessageW(g.internal, BM_GETCHECK, 0, 0) == BST_CHECKED;

    // Auswahl merken (Nummer + Seriennummer + Größe)
    DriveInfo selected;
    bool haveSelected = false;
    const LRESULT oldSel = SendMessageW(g.drive, CB_GETCURSEL, 0, 0);
    if (oldSel != CB_ERR && static_cast<size_t>(oldSel) < g.drives.size()) {
        selected = g.drives[static_cast<size_t>(oldSel)];
        haveSelected = true;
    }

    const bool wasEmpty = g.drives.empty();  // nur dann wird automatisch ausgewählt
    std::vector<DriveInfo> fresh;
    for (const DriveInfo& d : listDrives())
        if (isSelectable(d, includeInternal)) fresh.push_back(d);
    const std::vector<std::string> freshTexts = describeDrives(fresh);

    if (automatic) {
        for (size_t i = 0; i < fresh.size(); ++i)
            if (findDrive(g.drives, fresh[i]) < 0) appendLog(L"Laufwerk angeschlossen: " + toWide(freshTexts[i]));
        for (size_t i = 0; i < g.drives.size(); ++i)
            if (findDrive(fresh, g.drives[i]) < 0) appendLog(L"Laufwerk entfernt: " + toWide(g.driveTexts[i]));
        bool identical = freshTexts == g.driveTexts && fresh.size() == g.drives.size();
        for (size_t i = 0; identical && i < fresh.size(); ++i) identical = sameDrive(fresh[i], g.drives[i]);
        if (identical) {  // unverändert: Combobox nicht anfassen (Dropdown bleibt offen)
            g.drives = fresh;
            setRunning(false);
            return;
        }
    }

    g.drives = fresh;
    g.driveTexts = freshTexts;
    SendMessageW(g.drive, CB_RESETCONTENT, 0, 0);
    for (const std::string& t : g.driveTexts)
        SendMessageW(g.drive, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(toWide(t).c_str()));
    if (haveSelected) {
        // Nie stillschweigend ein anderes Laufwerk wählen: verschwunden = keine Auswahl.
        const int keep = findDrive(g.drives, selected);
        if (keep >= 0) {
            SendMessageW(g.drive, CB_SETCURSEL, keep, 0);
        } else {
            SendMessageW(g.drive, CB_SETCURSEL, -1, 0);
            appendLog(L"Ausgewähltes Laufwerk entfernt – bitte neu auswählen.");
        }
    } else if (wasEmpty && !g.drives.empty()) {
        SendMessageW(g.drive, CB_SETCURSEL, 0, 0);
    }
    // Aufgeklappte Liste mindestens so breit wie der breiteste Eintrag.
    {
        RECT rc;
        GetWindowRect(g.drive, &rc);
        int width = rc.right - rc.left;
        HDC dc = GetDC(g.drive);
        if (dc) {
            HGDIOBJ old = SelectObject(dc, reinterpret_cast<HGDIOBJ>(SendMessageW(g.drive, WM_GETFONT, 0, 0)));
            int widest = 0;
            for (const std::string& t : g.driveTexts) {
                const std::wstring w = toWide(t);
                SIZE sz = {};
                if (GetTextExtentPoint32W(dc, w.c_str(), static_cast<int>(w.size()), &sz)) widest = std::max<int>(widest, sz.cx);
            }
            SelectObject(dc, old);
            ReleaseDC(g.drive, dc);
            width = std::max(width, widest + 2 * GetSystemMetrics(SM_CXEDGE) + GetSystemMetrics(SM_CXVSCROLL) + 8);
        }
        SendMessageW(g.drive, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(width), 0);
    }
    setRunning(false);
    if (!automatic)
        appendLog(g.drives.empty() ? L"Kein passendes Laufwerk gefunden."
                                   : std::to_wstring(g.drives.size()) + L" Laufwerk(e) gefunden.");
}

// Nachholen einer während Lauf/Dialog zurückgestellten automatischen Aktualisierung.
void flushPendingRefresh() {
    if (!g.refreshPending || g.running || g.modal > 0 || g.closing) return;
    g.refreshPending = false;
    refreshDrives(true);
}

bool confirmTextMatches(HWND dlg) {
    wchar_t buf[32] = {};
    GetDlgItemTextW(dlg, IDC_CONFIRM_EDIT, buf, 32);
    return std::wcscmp(buf, kConfirmWord) == 0;
}

HICON loadAppIcon(int cx, int cy) {
    return static_cast<HICON>(LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, cx, cy, LR_SHARED));
}

INT_PTR CALLBACK confirmProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        const auto* info = reinterpret_cast<const ConfirmInfo*>(lp);
        SetWindowTextW(dlg, L"Löschen bestätigen");
        SendMessageW(dlg, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(loadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))));
        std::wstring partitions;
        for (size_t i = 0; i < info->volumes.size(); ++i) partitions += (i == 0 ? L"" : L", ") + toWide(info->volumes[i]);
        partitions = partitions.empty() ? L"Alle Partitionen auf diesem Laufwerk werden gelöscht (keine mit Laufwerksbuchstaben)."
                                        : L"Alle Partitionen auf diesem Laufwerk werden gelöscht: " + partitions;
        const std::wstring text = L"ALLE Daten auf diesem Laufwerk werden unwiederbringlich überschrieben:\r\n\r\n" +
                                  toWide(info->text) + L"\r\n\r\n" + partitions +
                                  L"\r\n\r\nZur Bestätigung LÖSCHEN eintippen:";
        SetDlgItemTextW(dlg, IDC_CONFIRM_TEXT, text.c_str());
        SetDlgItemTextW(dlg, IDOK, L"Löschen");
        SetDlgItemTextW(dlg, IDCANCEL, L"Abbrechen");
        EnableWindow(GetDlgItem(dlg, IDOK), FALSE);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_CONFIRM_EDIT && HIWORD(wp) == EN_CHANGE) {
            EnableWindow(GetDlgItem(dlg, IDOK), confirmTextMatches(dlg));
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            if (confirmTextMatches(dlg)) EndDialog(dlg, IDOK);  // Enter bei falschem Text ignorieren
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Setzt Balken, Text im Balken und (über setTaskbarProgress) die Taskleiste. permille: 0..1000.
void setOverallProgress(int permille);

void applyTaskbar() {
    if (!g.taskbar) return;
    g.taskbar->SetProgressState(g.wnd, g.taskbarState);
    if (g.taskbarState == TBPF_NORMAL) g.taskbar->SetProgressValue(g.wnd, g.taskbarDone, 1000);
}

// Fehlt das Interface (z.B. vor TaskbarButtonCreated), wird der Zustand gemerkt und später angewendet.
void setTaskbarProgress(TBPFLAG state, ULONGLONG done = 0) {
    g.taskbarState = state;
    g.taskbarDone = done;
    applyTaskbar();
}

void createTaskbarList() {
    if (g.taskbar) {  // Explorer-Neustart: Nachricht kommt erneut
        applyTaskbar();
        return;
    }
    ITaskbarList3* list = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
                                reinterpret_cast<void**>(&list))) ||
        !list)
        return;
    if (FAILED(list->HrInit())) {
        list->Release();
        return;
    }
    g.taskbar = list;
    applyTaskbar();
}

void setOverallProgress(int permille) {
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    SendMessageW(g.progress, PBM_SETPOS, static_cast<WPARAM>(permille), 0);
    g.overallPercent = permille / 10;
    InvalidateRect(g.progress, nullptr, FALSE);
    if (g.taskbarState == TBPF_NORMAL) {
        g.taskbarDone = static_cast<ULONGLONG>(permille);
        if (g.taskbar) g.taskbar->SetProgressValue(g.wnd, g.taskbarDone, 1000);
    }
}

HTHEME g_progressTheme = nullptr;

void fillRectColor(HDC dc, const RECT& r, int sysColor) {
    HBRUSH b = GetSysColorBrush(sysColor);
    FillRect(dc, &r, b);
}

void drawProgressText(HDC dc, const RECT& textRc, const RECT& clip, COLORREF color, const std::wstring& text) {
    if (clip.right <= clip.left) return;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    SetTextColor(dc, color);
    RECT rc = textRc;
    DrawTextW(dc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RestoreDC(dc, saved);
}

// Zeichnet den Balken komplett selbst (doppelt gepuffert), inklusive Text "Gesamt: N %".
void paintProgress(HWND wnd, HDC target) {
    RECT rc;
    GetClientRect(wnd, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    HDC memDc = CreateCompatibleDC(target);
    HBITMAP bmp = memDc ? CreateCompatibleBitmap(target, w, h) : nullptr;
    const bool buffered = memDc && bmp;
    HDC dc = buffered ? memDc : target;  // Fallback: direkt auf das Ziel zeichnen
    HGDIOBJ oldBmp = buffered ? SelectObject(dc, bmp) : nullptr;
    HGDIOBJ oldFont = SelectObject(dc, g.font);
    const int oldBk = SetBkMode(dc, TRANSPARENT);
    fillRectColor(dc, rc, COLOR_BTNFACE);

    LRESULT pos = SendMessageW(wnd, PBM_GETPOS, 0, 0);
    if (pos < 0) pos = 0;
    if (pos > 1000) pos = 1000;
    RECT inner = rc;
    HIGHCONTRASTW hc = {};
    hc.cbSize = sizeof(hc);
    const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    const bool themed = g_progressTheme != nullptr && !highContrast;
    if (themed) {
        DrawThemeBackground(g_progressTheme, dc, PP_BAR, 0, &rc, nullptr);
        GetThemeBackgroundContentRect(g_progressTheme, dc, PP_BAR, 0, &rc, &inner);
    } else {
        DrawEdge(dc, &inner, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
    }
    RECT filled = inner;
    filled.right = inner.left + static_cast<LONG>((inner.right - inner.left) * pos / 1000);
    if (filled.right > filled.left) {
        if (themed)
            DrawThemeBackground(g_progressTheme, dc, PP_FILL, PBFS_NORMAL, &filled, nullptr);
        else
            fillRectColor(dc, filled, COLOR_HIGHLIGHT);
    }
    if (!themed) {
        RECT rest = inner;
        rest.left = filled.right;
        if (rest.right > rest.left) fillRectColor(dc, rest, COLOR_WINDOW);
    }

    if (g.overallPercent >= 0) {
        const std::wstring text = L"Gesamt: " + std::to_wstring(g.overallPercent) + L" %";
        RECT restClip = inner;
        restClip.left = filled.right;
        drawProgressText(dc, rc, filled, themed ? RGB(0, 0, 0) : GetSysColor(COLOR_HIGHLIGHTTEXT), text);
        drawProgressText(dc, rc, restClip, GetSysColor(COLOR_WINDOWTEXT), text);
    }

    if (buffered) BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SetBkMode(dc, oldBk);
    if (buffered) SelectObject(dc, oldBmp);
    if (bmp) DeleteObject(bmp);
    if (memDc) DeleteDC(memDc);
}

LRESULT CALLBACK progressSubclass(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        if (wp) {
            paintProgress(wnd, reinterpret_cast<HDC>(wp));
        } else {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(wnd, &ps);
            if (dc) paintProgress(wnd, dc);
            EndPaint(wnd, &ps);
        }
        return 0;
    }
    case WM_PRINTCLIENT:
        paintProgress(wnd, reinterpret_cast<HDC>(wp));
        return 0;
    case WM_THEMECHANGED:
        if (g_progressTheme) CloseThemeData(g_progressTheme);
        g_progressTheme = OpenThemeData(wnd, L"PROGRESS");
        InvalidateRect(wnd, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        if (g_progressTheme) CloseThemeData(g_progressTheme);
        g_progressTheme = nullptr;
        RemoveWindowSubclass(wnd, progressSubclass, id);
        break;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

void onProgress() {
    Progress p;
    {
        std::lock_guard<std::mutex> lock(g.progressMutex);
        p = g.lastProgress;
    }
    if (p.total == 0 || p.totalPasses == 0) return;
    const int phasesPerPass = g.wipeMode ? 2 : 1;
    const int phaseIndex = (g.wipeMode && p.phase == Phase::Verify) ? 1 : 0;
    const double unitsTotal = double(p.totalPasses) * phasesPerPass * double(p.total);
    const double unitsDone = double((p.pass - 1) * phasesPerPass + phaseIndex) * double(p.total) + double(p.done);
    setOverallProgress(static_cast<int>(unitsDone * 1000.0 / unitsTotal));

    const double elapsed = std::chrono::duration<double>(Clock::now() - g.started).count();
    const double speed = elapsed > 0 ? unitsDone / elapsed : 0;
    std::wstring text = L"Durchgang " + std::to_wstring(p.pass) + L"/" + std::to_wstring(p.totalPasses) + L" – " +
                        (p.phase == Phase::Write ? L"Schreiben" : L"Prüfen") + L" – " +
                        std::to_wstring(p.done * 100 / p.total) + L" %";
    if (speed > 0)
        text += L" – " + germanDecimal(speed / 1e6) + L" MB/s – Rest ca. " + formatDuration((unitsTotal - unitsDone) / speed);
    SetWindowTextW(g.status, text.c_str());
}

void startOperation(bool wipe) {
    const LRESULT sel = SendMessageW(g.drive, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR || sel >= static_cast<LRESULT>(g.drives.size())) return;
    const DriveInfo drive = g.drives[static_cast<size_t>(sel)];
    const std::string driveText = g.driveTexts[static_cast<size_t>(sel)];
    const ConfirmInfo confirmInfo{driveText, drive.volumes};
    int randomPasses = static_cast<int>(SendMessageW(g.spin, UDM_GETPOS32, 0, 0));
    if (randomPasses < 0) randomPasses = 0;
    if (randomPasses > kMaxRandomPasses) randomPasses = kMaxRandomPasses;

    if (wipe) {
        INT_PTR confirmed;
        {
            ModalGuard guard;
            confirmed = DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_CONFIRM), g.wnd, confirmProc,
                                        reinterpret_cast<LPARAM>(&confirmInfo));
        }
        if (confirmed != IDOK) {
            appendLog(L"Löschen nicht bestätigt.");
            flushPendingRefresh();
            return;
        }
    }

    if (g.worker.joinable()) g.worker.join();
    g.wipeMode = wipe;
    g.cancel = false;
    g.started = Clock::now();
    g.lastPost = Clock::time_point{};
    setTaskbarProgress(TBPF_NORMAL, 0);
    setOverallProgress(0);
    setResult(L"", RGB(0, 0, 0));
    SetWindowTextW(g.status, L"Laufwerk wird geöffnet …");
    appendLog((wipe ? L"Löschen gestartet: " : L"Prüfung gestartet: ") + toWide(driveText) +
              (wipe ? L" (" + std::to_wstring(randomPasses) + L"× Zufall + 1× Nullen)" : L""));
    setRunning(true);

    g.worker = std::thread([drive, wipe, randomPasses] {
        std::unique_ptr<Result> result;
        try {
            result.reset(new Result());
            std::string err;
            std::unique_ptr<WinPhysicalDevice> dev = WinPhysicalDevice::open(drive, err);
            if (!dev) {
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
                *result = wipe ? runWipe(*dev, randomPasses, report, g.cancel) : runVerifyZero(*dev, report, g.cancel);
            }
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

void onDone(Result* raw) {
    std::unique_ptr<Result> r(raw);
    if (g.worker.joinable()) g.worker.join();
    setRunning(false);
    SetWindowTextW(g.status, L"Bereit");

    switch (r->status) {
    case Status::Success: {
        setOverallProgress(1000);
        setTaskbarProgress(TBPF_NOPROGRESS);
        std::wstring text = L"Erfolg: alle " + std::to_wstring(r->bytesTotal) + L" Bytes = 0x00";
        if (g.wipeMode) text += L" (" + std::to_wstring(r->passesCompleted) + L" Durchgänge geschrieben und verifiziert)";
        setResult(text, RGB(0, 128, 0));
        appendLog(text);
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
        const std::wstring text = g.wipeMode ? L"Abgebrochen – Datenträger unvollständig gelöscht" : L"Prüfung abgebrochen";
        setTaskbarProgress(TBPF_PAUSED);
        setResult(text, RGB(200, 110, 0));
        appendLog(text);
        break;
    }
    case Status::IoError: {
        const std::wstring text = L"Fehler: " + toWide(r->message);
        setTaskbarProgress(TBPF_ERROR);
        setResult(text, RGB(190, 0, 0));
        appendLog(text);
        break;
    }
    case Status::VerifyMismatch: {
        std::wstring text = L"Prüfung fehlgeschlagen";
        setTaskbarProgress(TBPF_ERROR);
        if (g.wipeMode) text += L" in Durchgang " + std::to_wstring(r->failedPass);
        text += L": " + std::to_wstring(r->mismatchCount) + L" abweichende Bytes, erste bei Offset " +
                std::to_wstring(r->firstMismatch);
        setResult(text, RGB(190, 0, 0));
        appendLog(text);
        appendLog(L"Daten ab Offset " + std::to_wstring(r->firstMismatch) + L": " + hexExcerpt(r->excerpt));
        break;
    }
    }

    if (g.closing) {
        DestroyWindow(g.wnd);
        return;
    }
    if (g.wipeMode && g.modal > 0) {
        g.refreshPending = true;  // Dialog offen: Nachholen beim Schließen
    } else if (g.wipeMode) {
        const bool automatic = g.refreshPending;
        g.refreshPending = false;
        refreshDrives(automatic);
    } else {
        flushPendingRefresh();
    }
}

HWND makeChild(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD exStyle = 0) {
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g.wnd,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
}

void createControls() {
    makeChild(WC_STATICW, L"Laufwerk:", SS_LEFT, 12, 16, 70, 20, 0);
    g.drive = makeChild(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 85, 12, 280, 200, IDC_DRIVE);
    g.refresh = makeChild(WC_BUTTONW, L"Aktualisieren", BS_PUSHBUTTON | WS_TABSTOP, 372, 11, 96, 26, IDC_REFRESH);
    g.internal = makeChild(WC_BUTTONW, L"Interne Laufwerke anzeigen", BS_AUTOCHECKBOX | WS_TABSTOP, 85, 44, 280, 20, IDC_INTERNAL);

    makeChild(WC_STATICW, L"Zufallsdurchgänge:", SS_LEFT, 12, 76, 110, 20, 0);
    g.passes = makeChild(WC_EDITW, L"3", ES_NUMBER | ES_RIGHT | WS_TABSTOP, 125, 72, 50, 24, IDC_PASSES, WS_EX_CLIENTEDGE);
    g.spin = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
                             WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                             0, 0, 0, 0, g.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPIN)), g.inst, nullptr);
    SendMessageW(g.spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(g.passes), 0);
    SendMessageW(g.spin, UDM_SETRANGE32, 0, kMaxRandomPasses);
    SendMessageW(g.spin, UDM_SETPOS32, 0, 3);
    makeChild(WC_STATICW, L"+ 1 Nulldurchgang", SS_LEFT, 185, 76, 200, 20, 0);

    g.wipeBtn = makeChild(WC_BUTTONW, L"Löschen", BS_PUSHBUTTON | WS_TABSTOP, 12, 106, 100, 30, IDC_WIPE);
    g.verifyBtn = makeChild(WC_BUTTONW, L"Prüfen", BS_PUSHBUTTON | WS_TABSTOP, 120, 106, 100, 30, IDC_VERIFY);
    g.cancelBtn = makeChild(WC_BUTTONW, L"Abbrechen", BS_PUSHBUTTON | WS_TABSTOP, 228, 106, 100, 30, IDC_CANCEL);

    g.progress = makeChild(PROGRESS_CLASSW, L"", 0, 12, 148, 456, 22, IDC_PROGRESS);
    g_progressTheme = OpenThemeData(g.progress, L"PROGRESS");
    SetWindowSubclass(g.progress, progressSubclass, 1, 0);
    SendMessageW(g.progress, PBM_SETRANGE32, 0, 1000);
    g.status = makeChild(WC_STATICW, L"Bereit", SS_LEFT | SS_ENDELLIPSIS, 12, 176, 456, 20, IDC_STATUS);
    g.result = makeChild(WC_STATICW, L"", SS_LEFT, 12, 198, 456, 36, IDC_RESULT);
    g.log = makeChild(WC_EDITW, L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 12, 238, 456, 118, IDC_LOG,
                      WS_EX_CLIENTEDGE);

    EnumChildWindows(
        g.wnd,
        [](HWND child, LPARAM font) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(g.font));
}

LRESULT CALLBACK wndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g.taskbarCreatedMsg && msg == g.taskbarCreatedMsg) {
        createTaskbarList();
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        g.wnd = wnd;
        createControls();
        appendLog(L"diskwipe " DW_VERSION_WSTR L" bereit.");
        refreshDrives();
        {
            DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
            filter.dbcc_size = sizeof(filter);
            filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            filter.dbcc_classguid = kDiskInterfaceGuid;
            g.devNotify = RegisterDeviceNotificationW(wnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
            if (!g.devNotify)
                appendLog(L"Automatische Laufwerkserkennung nicht verfügbar: " + toWide(winErrorText(GetLastError())));
        }
        return 0;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE || wp == DBT_DEVNODES_CHANGED)
            SetTimer(wnd, IDT_REFRESH, kRefreshDebounceMs, nullptr);  // Neustart = Entprellung
        return TRUE;
    case WM_TIMER:
        if (wp == IDT_REFRESH) {
            KillTimer(wnd, IDT_REFRESH);
            if (g.running || g.modal > 0) g.refreshPending = true;
            else refreshDrives(true);
            return 0;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REFRESH:
            refreshDrives();
            return 0;
        case IDC_INTERNAL:
            if (HIWORD(wp) == BN_CLICKED) {
                if (SendMessageW(g.internal, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    int answer;
                    {
                        ModalGuard guard;
                        answer = MessageBoxW(wnd,
                                             L"Interne Laufwerke anzeigen?\n\nDas Löschen einer internen Festplatte "
                                             L"vernichtet alle Daten darauf. Die Systemplatte wird nie angezeigt.",
                                             L"Warnung", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                    }
                    if (answer != IDYES) SendMessageW(g.internal, BM_SETCHECK, BST_UNCHECKED, 0);
                }
                g.refreshPending = false;  // die manuelle Aktualisierung unten deckt es ab
                refreshDrives();
            }
            return 0;
        case IDC_DRIVE:
            if (HIWORD(wp) == CBN_SELCHANGE) setRunning(g.running);
            return 0;
        case IDC_WIPE:
            startOperation(true);
            return 0;
        case IDC_VERIFY:
            startOperation(false);
            return 0;
        case IDC_CANCEL:
            g.cancel = true;
            EnableWindow(g.cancelBtn, FALSE);
            appendLog(L"Abbruch angefordert …");
            return 0;
        }
        break;
    case WM_APP_PROGRESS:
        onProgress();
        return 0;
    case WM_APP_DONE:
        onDone(reinterpret_cast<Result*>(lp));
        return 0;
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == g.result) {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, g.resultColor);
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    case WM_CLOSE:
        if (g.running) {
            int answer;
            {
                ModalGuard guard;
                answer = MessageBoxW(wnd,
                                     L"Es läuft noch ein Vorgang. Abbrechen und beenden?\n"
                                     L"Der Datenträger ist dann unvollständig gelöscht.",
                                     L"diskwipe", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            }
            if (answer == IDYES) {
                if (!g.running) {  // Vorgang endete, während die Abfrage offen war
                    DestroyWindow(wnd);
                    return 0;
                }
                g.closing = true;
                g.cancel = true;
                appendLog(L"Beenden: Abbruch angefordert …");
            } else {
                flushPendingRefresh();
            }
            return 0;
        }
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        KillTimer(wnd, IDT_REFRESH);
        if (g.devNotify) UnregisterDeviceNotification(g.devNotify);
        g.devNotify = nullptr;
        if (g.taskbar) g.taskbar->Release();
        g.taskbar = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g.inst = inst;
    if (!isProcessElevated()) {
        MessageBoxW(nullptr, L"diskwipe benötigt Administratorrechte.", L"diskwipe", MB_ICONERROR);
        return 1;
    }

    const bool comOk = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&icc);

    HDC screen = GetDC(nullptr);
    g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = loadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = loadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"DiskwipeWindow";
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, S(480), S(368)};
    AdjustWindowRect(&rc, style, FALSE);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"diskwipe " DW_VERSION_WSTR, style, CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    if (!wnd) {
        if (comOk) CoUninitialize();
        return 1;
    }
    // Der Prozess läuft erhöht, Explorer nicht: die Nachricht muss explizit durchgelassen werden.
    g.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarButtonCreated");
    if (g.taskbarCreatedMsg) ChangeWindowMessageFilterEx(wnd, g.taskbarCreatedMsg, MSGFLT_ALLOW, nullptr);
    ShowWindow(wnd, show);
    UpdateWindow(wnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (g.worker.joinable()) g.worker.join();
    DeleteObject(g.font);
    if (comOk) CoUninitialize();
    return 0;
}
