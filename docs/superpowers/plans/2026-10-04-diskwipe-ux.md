# diskwipe UX-Iteration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Feedback aus dem ersten manuellen Test umsetzen: Laufwerke unterscheidbar anzeigen, Liste automatisch aktualisieren, App-Icon, Gesamtfortschritt im Balken und in der Taskleiste.

**Architecture:** Anzeige-Logik (Beschreibung, Duplikat-Erkennung) bleibt portabel in `drive.cpp` und ist unter Linux getestet. Windows-spezifisches (Volume-Buchstaben, Geräte-Benachrichtigung, Taskleiste, Icon) lebt in den `*_win.cpp`-Dateien bzw. Ressourcen.

**Tech Stack:** C++17, Win32 (RegisterDeviceNotificationW, SetWindowSubclass, ITaskbarList3), Python 3 + Pillow (Icon-Erzeugung), NSIS.

**Spec:** `docs/superpowers/specs/2026-10-04-diskwipe-design.md` (Abschnitt GUI); Nutzerentscheidungen aus dem Chat sind unten unter "Global Constraints" festgehalten.

## Global Constraints

- Alle bisherigen Global Constraints aus `docs/superpowers/plans/2026-10-04-diskwipe.md` gelten weiter (C++17, MinGW posix, statisch, deutsche Texte, W-APIs, Sicherheitsregeln).
- Tests dürfen niemals `WinPhysicalDevice::open` aufrufen oder ein Laufwerk mit Schreibzugriff öffnen; `diskwipe.exe` und Installer werden nicht gestartet.
- Anzeigeformat Laufwerk: `Disk <N> – <Modell> – <Größe> – <USB|Wechseldatenträger|Intern>`, gefolgt von ` – <Volumes>` wenn Volumes mit Buchstaben existieren, z.B. ` – E: (MEINSTICK), F:`. Volume ohne Bezeichnung: nur `E:`.
- Haben mehrere angezeigte Laufwerke dieselbe Basisbeschreibung (Modell + Größe + Typ), wird hinter dem Modell ` [SN …<letzte 4 Zeichen>]` ergänzt, sofern eine Seriennummer existiert und sich die Seriennummern unterscheiden.
- Automatische Liste: Ein-/Abstecken aktualisiert die Liste ca. 500 ms nach dem letzten Ereignis (Entprellung); Auswahl bleibt erhalten, wenn dasselbe Laufwerk (Nummer + Seriennummer + Größe) noch da ist; Log: "Laufwerk angeschlossen: …" / "Laufwerk entfernt: …". Während eines Laufs keine Aktualisierung; danach einmal nachholen.
- Gesamtfortschritt steht als Text **im** grünen Balken, zentriert: `Gesamt: 37 %`. Statuszeile darunter behält Durchgang, Schritt-Prozent, MB/s, Restzeit.
- Taskleiste: Fortschritt grün (normal) während des Laufs, rot (Fehler) bei IoError/VerifyMismatch bis zum nächsten Start, gelb (pausiert-Optik) bei Abbruch, keine Anzeige im Leerlauf/nach Erfolg.
- Icon: eigenes Icon (USB-Stick + Lösch-Symbol), Größen 16, 20, 24, 32, 40, 48, 64, 256; in Fenster (groß + klein), Taskleiste, Exe-Datei, Startmenü-Verknüpfung (über Exe), Installer und Deinstaller.

## Review Focus

1. Gerätebenachrichtigung während eines Laufs darf die Liste nicht neu aufbauen (Auswahl/Index würden sonst nicht mehr zum laufenden Laufwerk passen).
2. Volume-Enumeration darf keine Volumes mit Schreibzugriff öffnen (nur Zugriff 0 / Pfadabfragen).
3. Duplikat-Kennzeichnung bei identischen Seriennummern (Billig-Klone) → kein Suffix, kein Absturz.
4. Text im Fortschrittsbalken bleibt bei 0 %, 100 % und während Neuzeichnen lesbar; kein Flackern-Dauerschleife (kein InvalidateRect aus WM_PAINT).
5. ITaskbarList3 nur nach `TaskbarButtonCreated` benutzen; COM-Init/Release korrekt; Fehlschlag ist nicht fatal.

---

### Task 1: Laufwerke unterscheidbar anzeigen

**Files:**
- Modify: `src/drive.h`, `src/drive.cpp`, `tests/test_drive.cpp`, `src/enumerate_win.cpp`, `src/enumerate_win.h` (falls nötig), `src/gui_win.cpp` (nur die Stelle, die Beschreibungen in die Combobox schreibt)
- Test: `tests/test_drive.cpp`, `tests/win/test_enumerate.cpp`

**Interfaces:**
- Produces:
  - `DriveInfo` erhält `std::vector<std::string> volumes;` — je Eintrag `"E:"` oder `"E: (LABEL)"` (UTF-8), sortiert nach Buchstabe.
  - `std::string describeDrive(const DriveInfo& d);` — Basisformat plus ` – <volumes, kommagetrennt>` wenn nicht leer (bestehende Tests ohne Volumes bleiben unverändert gültig).
  - `std::vector<std::string> describeDrives(const std::vector<DriveInfo>& drives);` — gleiche Reihenfolge; ergänzt bei Duplikaten (gleiches Modell + gleiche Größe + gleicher Typ) ` [SN …XXXX]` hinter dem Modell, wenn das Laufwerk eine Seriennummer hat und sich die Seriennummern der Duplikate unterscheiden.
- Consumes: `listDrives()`, `queryDrive()` (enumerate_win), `volumeDiskNumbers()` (util_win).

- [ ] **Step 1: Failing Tests in `tests/test_drive.cpp`**
  - `describe_drive_lists_volumes`: Volumes `{"E: (MEINSTICK)", "F:"}` → `"Disk 3 – VendorCo ProductCode – 14,6 GB – USB – E: (MEINSTICK), F:"` (Größe 15728640000).
  - `describe_drives_marks_duplicates_with_serial`: zwei Laufwerke gleiches Modell/Größe/USB, Seriennummern `"AAAA1111"` und `"BBBB2222"` → beide enthalten ` [SN …1111]` bzw. ` [SN …2222]` direkt hinter dem Modell; ein drittes, anderes Laufwerk bleibt ohne Suffix.
  - `describe_drives_identical_serials_no_suffix`: zwei Laufwerke mit identischer Seriennummer → kein Suffix (Klone).
  - `describe_drives_empty_serial_no_suffix`.
- [ ] **Step 2:** `make test-unit` → Compile-Fehler/Fehlschlag (RED) festhalten.
- [ ] **Step 3:** In `drive.cpp` implementieren (Basisbeschreibung intern einmal bauen, kein duplizierter Formatierungscode).
- [ ] **Step 4:** In `enumerate_win.cpp` `volumes` füllen: `FindFirstVolumeW`/`FindNextVolumeW`, Volume mit Zugriff 0 öffnen (`CreateFileW(path ohne abschließenden Backslash, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, …)`), `volumeDiskNumbers` → wenn das Volume genau auf Laufwerk N liegt: `GetVolumePathNamesForVolumeNameW` (Buchstaben wie `E:\` → `E:`) und `GetVolumeInformationW` (Bezeichnung, darf fehlschlagen → ohne Bezeichnung). Volume-Map einmal pro `listDrives()`-Aufruf erstellen, nicht pro Laufwerk.
- [ ] **Step 5:** GUI-Combobox nutzt `describeDrives(g.drives)` statt Einzelaufrufen; der Bestätigungsdialog nutzt dieselbe Zeichenkette wie die Combobox (Index-gleich).
- [ ] **Step 6:** Windows-Test in `tests/win/test_enumerate.cpp`: `win_volumes_have_drive_letter_format` — jeder Eintrag in `volumes` beginnt mit `[A-Z]:`. `make test-unit`, `make test-win`, `make windows` grün, keine Warnungen.
- [ ] **Step 7:** Commit `feat: show drive letters, labels and serial suffix for duplicate drives`.

---

### Task 2: Automatische Laufwerksliste

**Files:**
- Modify: `src/gui_win.cpp`, ggf. `Makefile` (zusätzliche Link-Bibliothek nur falls nötig)

**Interfaces:**
- Consumes: `refreshDrives()` (gui_win), `listDrives()`, `describeDrives()` (Task 1).

- [ ] **Step 1:** In `WM_CREATE` `RegisterDeviceNotificationW` mit `DEV_BROADCAST_DEVICEINTERFACE_W` für `GUID_DEVINTERFACE_DISK` (`<initguid.h>` vor `<ntddstor.h>` bzw. `<winioctl.h>`, oder GUID lokal definieren `{53f56307-b6bf-11d0-94f2-00a0c91efb8b}`) registrieren; in `WM_DESTROY` `UnregisterDeviceNotification`.
- [ ] **Step 2:** `WM_DEVICECHANGE` mit `DBT_DEVICEARRIVAL`, `DBT_DEVICEREMOVECOMPLETE` (und `DBT_DEVNODES_CHANGED` als Fallback): `SetTimer(wnd, IDT_REFRESH, 500, nullptr)` (Neustart des Timers = Entprellung). `WM_TIMER`/`IDT_REFRESH`: `KillTimer`; läuft ein Vorgang (`g.running`), nur `g.refreshPending = true`; sonst Liste neu aufbauen.
- [ ] **Step 3:** `refreshDrives` erhält Auswahl-Erhalt: vorher gewähltes Laufwerk merken (Nummer + Seriennummer + Größe), nach dem Neuaufbau wieder auswählen, falls vorhanden; sonst Index 0 bzw. leer. Log-Zeilen für hinzugekommene / entfernte Laufwerke (Vergleich alte/neue Liste über Nummer + Seriennummer + Größe; Text mit der Beschreibung). Beim manuellen "Aktualisieren" bleibt die bisherige Log-Zeile "N Laufwerk(e) gefunden.".
- [ ] **Step 4:** In `onDone` nach Abschluss: wenn `g.refreshPending` oder Wipe-Modus → `refreshDrives()`; `g.refreshPending = false`.
- [ ] **Step 5:** `make windows` ohne Warnungen; `make test-unit`, `make test-win` grün. Commit `feat: refresh drive list automatically on device arrival/removal`.

---

### Task 3: App-Icon

**Files:**
- Create: `tools/make_icon.py`, `src/diskwipe.ico` (vom Skript erzeugt, eingecheckt)
- Modify: `src/resource.h`, `src/resource.rc`, `src/gui_win.cpp`, `installer/diskwipe.nsi`, `Makefile` (Abhängigkeit `build/resource.o` von `src/diskwipe.ico`), `.gitattributes` (`*.ico binary`)

- [ ] **Step 1:** `tools/make_icon.py` (Pillow): zeichnet bei 1024 px und skaliert mit LANCZOS auf 16, 20, 24, 32, 40, 48, 64, 256; speichert `src/diskwipe.ico` (`img.save(..., sizes=[...])`). Motiv: abgerundetes Quadrat, Hintergrund dunkles Blau (#1E3A5F) → Petrol (#0F766E) Verlauf; weißer USB-Stick schräg (Stecker oben, metallgrau #CBD5E1); unten rechts ein runder roter Badge (#DC2626) mit weißem "×" als Lösch-Symbol. Bei 16/20 px muss Stick + Badge erkennbar bleiben (kräftige Konturen, keine feinen Details). Zusätzlich `tools/icon-preview.png` (256 px) erzeugen, aber nicht einchecken (in `.gitignore`).
- [ ] **Step 2:** `resource.h`: `#define IDI_APP 1` (niedrigste ID → Explorer/Taskleiste nehmen es als Exe-Icon). `resource.rc`: `IDI_APP ICON "src/diskwipe.ico"` vor allen anderen Ressourcen.
- [ ] **Step 3:** `gui_win.cpp`: `wc.hIcon = LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0)`, `wc.hIconSm` analog mit `SM_CXSMICON`. Bestätigungsdialog: `WM_SETICON` mit dem kleinen Icon.
- [ ] **Step 4:** `installer/diskwipe.nsi`: `!define MUI_ICON` und `!define MUI_UNICON` auf das Icon (Pfad über `-DICON=` aus dem Makefile übergeben, wie `EXE`; `!ifndef ICON !error`). Verknüpfungen bekommen explizit `"$INSTDIR\diskwipe.exe" 0` als Icon. Makefile-`dist` übergibt `-DICON=$(abspath src/diskwipe.ico)`.
- [ ] **Step 5:** Prüfen: `python3 tools/make_icon.py` erzeugt die Datei; `make windows && make dist` grün; `x86_64-w64-mingw32-objdump -p build/diskwipe.exe` bzw. `strings` zeigen, dass eine ICON-Ressource enthalten ist (z.B. `x86_64-w64-mingw32-windres build/diskwipe.exe -O rc 2>/dev/null | grep -i icon` oder Ressourcen-Dump). Preview-PNG im Report beschreiben (Pfad nennen).
- [ ] **Step 6:** Commit `feat: add application icon`.

---

### Task 4: Gesamtfortschritt im Balken und in der Taskleiste

**Files:**
- Modify: `src/gui_win.cpp`, `Makefile` (`-lole32 -luuid` zur Exe falls nötig)
- Modify: `docs/manual-test.md` (neue Schritte), `README.md` (ein Satz zur automatischen Liste)

- [ ] **Step 1:** Fortschrittsbalken mit `SetWindowSubclass` (comctl32 v6) subclassen: `WM_PAINT` → `DefSubclassProc` zeichnen lassen, danach mit `GetDC`, Fenster-Font, `SetBkMode(TRANSPARENT)` den Text `Gesamt: N %` zentriert (`DrawTextW`, `DT_CENTER|DT_VCENTER|DT_SINGLELINE`) zeichnen. Textfarbe gut lesbar auf Grün und Grau (Schwarz/sehr dunkles Grau). Text nur, wenn ein Vorgang läuft oder abgeschlossen ist (im Leerlauf vor dem ersten Start leer). Prozentwert kommt aus einer Variablen (`g.overallPercent`, 0–100), die `onProgress`/`onDone`/`startOperation` setzen; danach `InvalidateRect(g.progress, nullptr, FALSE)` außerhalb von WM_PAINT. `WM_NCDESTROY` → `RemoveWindowSubclass`. Balkenhöhe auf 22 px (skaliert) erhöhen, nachfolgende Controls entsprechend um 4 px nach unten, Fensterhöhe anpassen.
- [ ] **Step 2:** Taskleiste: `RegisterWindowMessageW(L"TaskbarButtonCreated")`; bei dieser Nachricht `CoCreateInstance(CLSID_TaskbarList, …, IID_ITaskbarList3)` + `HrInit()`; `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` in `wWinMain`, `CoUninitialize` am Ende; Interface in `WM_DESTROY` freigeben. Hilfsfunktion `setTaskbarProgress(state, value)`: Start → `TBPF_NORMAL`, Fortschritt → `SetProgressValue(done, 1000)`, Erfolg → `TBPF_NOPROGRESS`, Abbruch → `TBPF_PAUSED` (bleibt bis nächster Start), Fehler → `TBPF_ERROR` (bleibt bis nächster Start). Fehlt das Interface, still ignorieren.
- [ ] **Step 3:** Statuszeile unverändert (Durchgang x/y – Phase – Schritt-% – MB/s – Rest).
- [ ] **Step 4:** `docs/manual-test.md`: Schritte ergänzen: Liste aktualisiert sich beim Ab-/Anstecken ohne Button (Auswahl bleibt bei anderem Stick erhalten); Laufwerksbuchstabe/Bezeichnung sichtbar; Icon in Fenster, Taskleiste, angeheftet, Startmenü, Installer; Balken zeigt `Gesamt: N %`, Taskleisten-Icon zeigt grünen Fortschritt, nach Abbruch gelb, nach Fehler rot. `README.md`: kurzer Satz, dass die Liste sich automatisch aktualisiert.
- [ ] **Step 5:** `make test-unit`, `make test-win`, `make windows`, `make dist` grün, keine Warnungen. Commit `feat: show overall progress inside the bar and on the taskbar button`.
