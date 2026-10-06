# diskwipe – Fortsetzen nach Abstecken & Protokolldatei

Datum: 2026-10-06

## Ziel

1. Wird ein Datenträger während des Löschens abgesteckt (oder bricht der Lauf mit einem I/O-Fehler ab), kann der Löschvorgang nach dem Wiedereinstecken an der unterbrochenen Stelle (Durchgang, Phase, Offset) fortgesetzt werden – solange diskwipe nicht geschlossen und kein anderer Vorgang gestartet wurde.
2. Jeder Lauf (Löschen und Prüfen) erzeugt eine Protokolldatei, mit der der Nutzer nachvollziehen kann, was passiert ist. Am Ende wird sie per "Speichern unter" angeboten.

## Grenzen (bewusst akzeptiert)

- Kein Fortsetzen nach Neustart des Programms; der Fortsetzen-Stand lebt nur im Speicher.
- Kein Fortsetzen ohne Seriennummer (Identität nicht sicher feststellbar).
- Keine Prüfsumme im Protokoll: Sie wäre nicht fälschungssicher (jeder kann sie nach einer Änderung neu berechnen).
- Seeds stehen nicht im Protokoll.

## Fortsetzen-Stand

```cpp
struct ResumePoint {
    int pass = 1;                 // 1-basiert
    Phase phase = Phase::Write;
    uint64_t offset = 0;          // ab hier weitermachen
    uint64_t failedAt = 0;        // Offset des fehlgeschlagenen Zugriffs (nur Write relevant)
    uint64_t mismatches = 0;      // bisher gezählte Abweichungen (nur Verify)
    uint64_t firstMismatch = 0;
    std::vector<uint8_t> excerpt;
};
```

- `runPasses(dev, plan, start, progress, events, cancel)` beginnt bei `start` (Standard: Durchgang 1, Write, Offset 0). `runWipe` erzeugt weiterhin den Plan und ruft `runPasses` mit Standard-Start.
- **Checkpoints in der Schreibphase:** Nach je 64 Blöcken (64 MiB) ruft `wiper` `dev.flush()` auf. Erst nach erfolgreichem Flush gilt dieser Offset als Checkpoint. USB-Sticks können Daten im eigenen Cache halten; was nach dem letzten Checkpoint geschrieben wurde, gilt beim Abstecken als verloren und wird erneut geschrieben.
- **Bei `Status::IoError`** enthält `Result` zusätzlich `std::optional<ResumePoint> resume`:
  - Write-Phase: `offset` = letzter Checkpoint, `failedAt` = fehlgeschlagener Offset. Flush-Fehler am Ende der Phase ebenso (letzter Checkpoint).
  - Verify-Phase: `offset` = Beginn des fehlgeschlagenen Blocks; bisherige Abweichungen werden übernommen und beim Fortsetzen weitergezählt.
- `Cancelled` und `VerifyMismatch` bleiben endgültig (kein `resume`).
- `runVerifyZero` bleibt ohne Fortsetzen.

## Ereignisse für das Protokoll

`wiper` erhält einen optionalen Callback `EventFn` (`std::function<void(const Event&)>`):

| Ereignis | Daten |
|---|---|
| `PhaseStarted` | Durchgang, Phase, Start-Offset |
| `PhaseCompleted` | Durchgang, Phase, Abweichungen (Verify) |

Unterbrechung und Endergebnis protokolliert die GUI aus dem `Result`.

## Identität & Schutz vor falschem Laufwerk

Nach dem Wiedereinstecken hat das Laufwerk meist eine andere Disk-Nummer.

1. **Identität** = Modell, Seriennummer (nicht leer), Größe, USB, Removable. Neue portable Funktion in `drive.{h,cpp}`:
   `std::vector<int> findByIdentity(const std::vector<DriveInfo>& list, const DriveInfo& id)` (Indizes, Nummer wird ignoriert).
2. **Eindeutigkeit:** Fortsetzen ist nur möglich, wenn genau ein Laufwerk passt. Bei mehreren: Status "Mehrere passende Laufwerke – das andere abziehen".
3. **Inhaltsprüfung vor dem ersten Schreibzugriff** (portabel in `wiper`, `checkResumeContent(dev, plan, resume)`): Das erwartete Muster ist über die Seeds bekannt. Gelesen werden höchstens zwei Blöcke:
   - **Block 0:** Ist `offset ≥` Ende von Block 0 (Write) bzw. Phase = Verify, muss Block 0 dem Muster des aktuellen Durchgangs entsprechen.
   - **Letzter Block:** Liegt `failedAt` (Write) vor dem Beginn des letzten Blocks und ist `pass > 1`, muss der letzte Block dem Muster von Durchgang `pass − 1` entsprechen.
   - Ein Treffer gegen ein **Zufallsmuster** ist ein starker Nachweis. Ein Treffer nur gegen das Nullmuster ist schwach.
   - Ergebnis: `Mismatch` (eine anwendbare Prüfung schlägt fehl) → kein Fortsetzen, Status "Inhalt passt nicht zum unterbrochenen Vorgang – falscher Stick?". `Strong` → Fortsetzen ohne weitere Rückfrage. `Weak` (keine starke Prüfung anwendbar, z.B. Durchgang 1 mit Checkpoint 0 oder Nulldurchgang mit bereits überschriebenem letzten Block) → derselbe Bestätigungsdialog wie bei neuem Löschen (`LÖSCHEN` eintippen).
   - In der Verify-Phase wird nur gelesen; ein falscher Stick führt dort zu `VerifyMismatch`, bevor ein weiterer Durchgang schreibt. Deshalb gibt es in der Verify-Phase nie den Bestätigungsdialog: `Weak` wird dort wie `Strong` behandelt, nur `Mismatch` stoppt.
4. Danach öffnet `WinPhysicalDevice::open` das aktuell gelistete `DriveInfo` (mit neuer Nummer) wie bisher; dessen Revalidierung bleibt unverändert.

## GUI

- Neuer Button **"Fortsetzen"** rechts neben "Abbrechen" (336, 106, 100×30), sonst deaktiviert.
- **Unterbrechung** (Löschen endet mit `IoError` und `resume`, Seriennummer nicht leer): Die GUI behält Plan, `ResumePoint`, Identität und Protokoll. Statuszeile: "Unterbrochen – Durchgang 2/4, Schreiben bei 37 % – Laufwerk wieder einstecken". Ergebnisfeld orange mit Fehlertext. Taskleiste: pausiert.
- **Wiedereinstecken:** Bei jeder Aktualisierung der Laufwerksliste (automatisch oder manuell) wird `findByIdentity` geprüft. Genau ein Treffer → Laufwerk auswählen, "Fortsetzen" aktivieren, Status "Laufwerk wieder erkannt – Fortsetzen klicken".
- **Fortsetzen klicken:** Identität und Eindeutigkeit erneut prüfen, im Worker: Gerät öffnen → `checkResumeContent` → ggf. Bestätigungsdialog (Weak; Dialog im GUI-Thread, Worker-Ablauf zweistufig) → `runPasses` ab `ResumePoint`. Schlägt das Öffnen fehl (z.B. "in Benutzung"), bleibt der Stand erhalten.
- Erneute Unterbrechung beim fortgesetzten Lauf ist wieder fortsetzbar.
- **Verwerfen** des Stands:
  - "Abbrechen" ist während eines offenen Fortsetzen-Stands aktiv → Rückfrage "Unterbrochenen Vorgang verwerfen?".
  - "Löschen"/"Prüfen" → dieselbe Rückfrage vor dem normalen Ablauf.
  - Fenster schließen → Rückfrage "Unterbrochener Vorgang geht verloren. Beenden?".
  - Nach dem Verwerfen: Protokoll mit "Vorgang verworfen – Datenträger unvollständig gelöscht" abschließen, "Speichern unter" anbieten.
- Während ein Stand offen ist, bleiben Laufwerksliste und Durchgänge-Feld bedienbar; die Durchgänge-Einstellung wirkt erst bei neuem Löschen.

## Protokolldatei

Neues portables Modul `src/audit_log.{h,cpp}`:

- `AuditLog::open(path)`, `line(text)`: schreibt `YYYY-MM-DD HH:MM:SS  text` + CRLF, UTF-8, und flusht nach jeder Zeile (übersteht Absturz/Stromausfall bis zur letzten Zeile).
- Reine Funktion `formatLogLine(std::tm, text)` für Tests.
- Ablage während des Laufs: `%TEMP%\diskwipe-<YYYYMMDD-HHMMSS>.log`.

Inhalt:

```
diskwipe 1.1.0 – Protokoll
Vorgang: Löschen, 3× Zufall + 1× Nullen
Laufwerk: Disk 3 – Modell – SN … – 15.728.640.000 Bytes – USB
Start Durchgang 1/4 (Zufall), Schreiben ab Offset 0
Ende Durchgang 1/4, Schreiben
Start Durchgang 1/4, Prüfen ab Offset 0
Ende Durchgang 1/4, Prüfen: 0 Abweichungen
Unterbrechung in Durchgang 2/4, Schreiben bei Offset 6.012.534.784: <Fehlertext>; letzter Checkpoint 5.972.688.896
Laufwerk wieder erkannt: Disk 4 (gleiche Seriennummer)
Inhaltsprüfung: Block 0 = Muster Durchgang 2, letzter Block = Muster Durchgang 1 → passt
Fortgesetzt in Durchgang 2/4, Schreiben ab Offset 5.972.688.896
…
Ergebnis: Erfolg – alle 15.728.640.000 Bytes = 0x00, 4 Durchgänge geschrieben und verifiziert, 1 Unterbrechung
```

- Endgültiges Ende (Erfolg, Abbruch, Prüffehler, Fehler ohne Fortsetzen, Verwerfen): letzte Zeile "Ergebnis: …", dann "Speichern unter" mit Vorschlag `diskwipe_<YYYY-MM-DD_HH-MM-SS>_<Modell>_SN<letzte 4>.log` im Ordner "Dokumente". Abbruch des Dialogs → Pfad der Temp-Datei im Log-Feld der GUI.
- Bei einer Unterbrechung wird kein Dialog gezeigt; das Protokoll läuft weiter.
- Beim Schließen während eines Laufs wird das Protokoll mit "Programm beendet" abgeschlossen; Speichern-Dialog wird angeboten.

## Komponenten (Änderungen)

| Datei | Änderung |
|---|---|
| `src/wiper.{h,cpp}` | `ResumePoint`, `Result::resume`, Checkpoint-Flush alle 64 MiB, Start ab `ResumePoint`, `EventFn`, `checkResumeContent` |
| `src/drive.{h,cpp}` | `findByIdentity` |
| `src/audit_log.{h,cpp}` | neu |
| `src/gui_win.cpp` | Fortsetzen-Zustand, Button, Verwerfen-Rückfragen, Protokoll, Speichern-Dialog |
| `tests/memory_device.h` | `flushCount`, Option "nicht geflushte Schreibzugriffe gehen bei Fehler verloren" |
| `Makefile` | `audit_log` in Linux-, Windows- und Testbuilds |
| `docs/manual-test.md`, `README.md` | Fortsetzen und Protokoll |

## Fehlerbehandlung

- Gerät beim Fortsetzen nicht öffenbar → Fehler im Status, Stand bleibt.
- Inhaltsprüfung: Lesefehler → wie nicht verfügbar behandeln, Stand bleibt.
- Protokolldatei nicht anlegbar/schreibbar → Warnung im Log-Feld, Lauf läuft trotzdem (Protokoll ist Zusatz, kein Grund, das Löschen zu verhindern).
- Speichern unter fehlgeschlagen → Fehlermeldung, Temp-Pfad im Log-Feld.

## Tests

### Automatisch (Linux und Windows-Build)

1. Schreibfehler in Durchgang 2 mitten in Block X → `resume` = letzter Checkpoint ≤ X, `failedAt` = X; Fortsetzen auf demselben Device → Erfolg, Daten alle 0x00.
2. Wie 1, aber `MemoryDevice` verwirft nicht geflushte Schreibzugriffe beim Fehler → Fortsetzen ab Checkpoint ergibt trotzdem Erfolg (Beleg, dass der Checkpoint konservativ genug ist).
3. Lesefehler in der Verify-Phase → `resume.phase = Verify`; Fortsetzen → Erfolg.
4. Manipuliertes Byte vor einem Verify-Lesefehler → nach dem Fortsetzen `VerifyMismatch` mit korrekter erster Position und Anzahl (Übernahme der Zählung).
5. Flush-Anzahl: Schreibphase über 200 MiB → mindestens 3 Checkpoint-Flushes.
6. `checkResumeContent`: gleiches Device → Strong; anderes Device mit Fremddaten → Mismatch; Durchgang 1 mit Checkpoint 0 → Weak; Nulldurchgang mit überschriebenem letzten Block → Weak.
7. `findByIdentity`: zwei baugleiche Sticks mit verschiedener SN → genau einer; gleiche SN → zwei; leere SN → keiner; andere Nummer → trotzdem Treffer.
8. `formatLogLine` und `AuditLog` (Zeilen landen geflusht in der Datei).
9. `EventFn`: Reihenfolge der Ereignisse bei 1 Zufall + 1 Null.

### Manuell (Windows)

1. Löschen starten, Stick in Durchgang 2 beim Schreiben abziehen → Status "Unterbrochen …", Fortsetzen deaktiviert.
2. Stick an anderem USB-Port einstecken → wird ausgewählt, Fortsetzen aktiv; klicken → läuft ab Checkpoint weiter, Ende grün.
3. Wie 1, dann baugleichen anderen Stick einstecken → "Inhalt passt nicht …", nichts geschrieben.
4. Wie 1, dann "Abbrechen" → Rückfrage, Protokoll, Speichern-Dialog.
5. Stick in der Prüfphase abziehen und fortsetzen → Erfolg.
6. Gespeichertes Protokoll enthält Unterbrechung, Fortsetzen und Ergebnis.

## Nicht im Umfang

Fortsetzen nach Programmneustart, Fortsetzen des reinen Prüfens, signierte Protokolle/Zertifikate, Protokoll als PDF.
