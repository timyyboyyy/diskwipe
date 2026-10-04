# diskwipe – Design

Datum: 2026-10-04

## Ziel

Natives Windows-Tool mit minimalistischer GUI, das einen kompletten Datenträger (primär USB-Sticks, typ. FAT32) roh Sektor für Sektor überschreibt und anschließend nachweist, dass der gesamte adressierbare Bereich nur noch 0x00 enthält. Das Dateisystem ist irrelevant, es wird mit überschrieben.

Der Stick wird danach nicht weiterverwendet; Verschleiß spielt keine Rolle. Priorität ist maximale Nicht-Wiederherstellbarkeit.

## Grenzen (bewusst akzeptiert)

- Flash-Controller haben Wear-Leveling und Reserveblöcke, die per Software nicht adressierbar sind. Überschreiben und Prüfen decken nur den vom OS sichtbaren Bereich ab. Mehrere Zufallsdurchgänge erhöhen die Wahrscheinlichkeit, dass auch rotierte Blöcke erfasst werden, garantieren es aber nicht.
- Für "nicht mal im Ansatz wiederherstellbar" gegen Chip-Off-Forensik: nach erfolgreichem Lauf physische Zerstörung empfohlen. Die GUI weist im Erfolgsdialog darauf hin.

## Plattform & Build

- C++17, Entwicklung in WSL.
- Windows-Build: Cross-Compile mit MinGW-w64 (`x86_64-w64-mingw32-g++`), statisch gelinkt, eine `diskwipe.exe`, Manifest mit `requireAdministrator`.
- Linux-Build: nur portabler Kern + Tests.
- Windows-Test-Build: `diskwipe_tests.exe` (portabler Kern + `device_file`), ausführbar über WSL-Interop.
- Build per `Makefile`: Ziele `windows`, `test`, `test-win`, `clean`.
- Projektordner: `~/diskwipe`.

## Löschverfahren

Standard: 3 Zufallsdurchgänge + 1 Nulldurchgang (Durchgänge in der GUI einstellbar: Anzahl Zufallsdurchgänge 0–10, der Nulldurchgang ist immer der letzte).

- Jeder Zufallsdurchgang erhält einen eigenen 64-Bit-Seed aus einer kryptografischen Quelle (`BCryptGenRandom` bzw. `/dev/urandom`).
- Zufallsmuster: xoshiro256** (Seed per splitmix64 expandiert). Das Muster ist positionsadressierbar: Jeder 1-MiB-Block wird aus (Seed, Blockindex) neu initialisiert, damit Schreiben und Prüfen identische Bytes erzeugen, unabhängig von Blockgrenzen am Ende.
- Nach jedem Durchgang: vollständiges Zurücklesen und Vergleich mit dem erwarteten Muster.
- Ergebnis nur dann "Erfolg", wenn alle Durchgänge vollständig geschrieben und fehlerfrei verifiziert wurden.

## Sicherheit gegen falsches Laufwerk

- Programm startet nur mit Adminrechten (Manifest; zusätzlich Laufzeitprüfung).
- Standardliste zeigt nur Laufwerke mit Bustyp USB oder Wechselmedium.
- Die Systemplatte (Laufwerk, das das Windows-Volume enthält) wird nie angezeigt.
- Andere interne Platten nur über Checkbox "Interne Laufwerke anzeigen" mit Warndialog.
- Vor Start: Bestätigungsdialog mit Modell, Größe, Laufwerksnummer; Nutzer muss `LÖSCHEN` eintippen.

## Komponenten

| Datei | Aufgabe | Plattform |
|---|---|---|
| `src/pattern.{h,cpp}` | `fillPattern(buf, len, kind, seed, blockIndex)`; `kind` ∈ {Zero, Random}; Seed-Erzeugung | portabel |
| `src/device.h` | abstrakte Klasse `BlockDevice`: `size()`, `sectorSize()`, `read(off, buf, n)`, `write(off, buf, n)`, `flush()` | portabel |
| `src/device_file.cpp` | `FileDevice` auf Image-Datei (Tests) | Linux/Windows |
| `src/device_win.cpp` | `WinPhysicalDevice`: Volumes des Laufwerks sperren + aushängen (`FSCTL_LOCK_VOLUME`, `FSCTL_DISMOUNT_VOLUME`), `\\.\PhysicalDriveN` mit `FILE_FLAG_NO_BUFFERING \| FILE_FLAG_WRITE_THROUGH` öffnen, Größe via `IOCTL_DISK_GET_LENGTH_INFO`, Sektorgröße via `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`, sektorausgerichtete Puffer (`VirtualAlloc`) | Windows |
| `src/enumerate_win.cpp` | `listDrives()`: Nummer, Modell, Größe, Bustyp, Removable, IsSystem (`IOCTL_STORAGE_QUERY_PROPERTY`, `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`) | Windows |
| `src/wiper.{h,cpp}` | `runWipe(dev, randomPasses, progressCb, cancelFlag)` und `runVerifyZero(dev, progressCb, cancelFlag)`; Blockgröße 1 MiB, letzter Block gekürzt (auf Sektorgröße ausgerichtet); Ergebnisstruktur mit Status, erster Abweichungsposition, Anzahl abweichender Bytes, Hex-Auszug (32 Byte) | portabel |
| `src/gui_win.cpp` | Win32-Fenster; Worker-Thread; Fortschritt per `PostMessage` | Windows |
| `src/diskwipe.manifest`, `src/resource.rc` | Admin-Manifest, Common Controls v6 | Windows |
| `tests/` | Testprogramm + Shell-Testskript | Linux/Windows |

`wiper` kennt nur `BlockDevice`, keine Windows-APIs. `gui_win` kennt `wiper`, `enumerate_win`, `device_win`.

## GUI

Ein Fenster (~480×360), Windows-Standardoptik (Common Controls v6):

- Dropdown "Laufwerk" (z.B. `Disk 2 – SanDisk Cruzer – 29,8 GB – USB`) + Button "Aktualisieren"
- Checkbox "Interne Laufwerke anzeigen"
- Spin-Feld "Zufallsdurchgänge" (Standard 3) + Hinweis "+ 1 Nulldurchgang"
- Buttons "Löschen", "Prüfen", "Abbrechen" (Abbrechen nur während Lauf aktiv)
- Fortschrittsbalken gesamt, Statuszeile: `Durchgang 2/4 – Prüfen – 41 % – 18,3 MB/s – Rest ca. 1:12 h`
- Log-Feld (mehrzeilig, nur lesen) mit Zeitstempeln
- Abschlussstatus: grün "Erfolg: alle N Bytes = 0x00" oder rot mit Fehlerdetails

Während eines Laufs sind Laufwerkswahl und Start-Buttons deaktiviert.

## Fehlerbehandlung

- Lese-/Schreibfehler: Abbruch, Anzeige Windows-Fehlercode + Byte-Offset. Fehlerhafte Sektoren werden nicht übersprungen; Ergebnis "nicht sicher gelöscht".
- Volume nicht sperrbar: Meldung "Laufwerk in Benutzung – Explorer-Fenster/Programme schließen", es wird nichts geschrieben.
- Abbrechen: stoppt nach aktuellem Block; Status "abgebrochen – unvollständig gelöscht".
- Fenster schließen während Lauf: Rückfrage, dann Abbruch und Warten auf Worker.
- Verifikationsabweichung (z.B. Fake-Kapazität): Fehlerstatus mit erster Position, Anzahl, Hex-Auszug.
- Kapazität 0 oder nicht ermittelbar: Abbruch vor dem Schreiben.

## Tests

### Automatisch (Linux, `make test`)

1. Muster: deterministisch (gleicher Seed+Block → gleiche Bytes), unterschiedliche Seeds → unterschiedliche Bytes, Zero-Muster nur 0x00, Zufallsmuster nicht trivial (Byte-Histogramm grob gleichverteilt).
2. Wipe auf Image mit Größe, die kein Vielfaches von 1 MiB ist (z.B. 64 MiB + 3 Sektoren): Ergebnis Erfolg.
3. Integration mit FAT32-Image: `mkfs.vfat` + `mcopy` von Testdateien (Text mit eindeutigen Markern, JPEG, PDF). Vorher: `grep` findet Marker, PhotoRec stellt Dateien wieder her. Nach Wipe: `grep -c` auf Marker = 0, `cmp` gegen `/dev/zero` (gleiche Länge) identisch, PhotoRec stellt 0 Dateien her.
4. Verify erkennt Manipulation: ein Byte nach dem Wipe ändern → Fehler mit exakt dieser Position, Anzahl 1.
5. Mock-Device mit Schreibfehler bei Offset X → Ergebnis Fehler, nicht Erfolg.
6. Mock-Device mit Lesefehler beim Verify → Fehler.
7. Mock-Device "Fake-Kapazität" (Adressen jenseits der echten Größe werden auf den Anfang umgelenkt) → Verify meldet Fehler.
8. Abbruch-Flag während Lauf → Status abgebrochen.

### Automatisch (Windows-Build über WSL-Interop, `make test-win`)

`diskwipe_tests.exe` führt Tests 1, 2, 4–8 mit `FileDevice` unter Windows aus.

### Manuell (Windows, Stick ohne wichtige Daten)

1. Stick vorher mit Dateien befüllen; PhotoRec bestätigt Wiederherstellbarkeit.
2. GUI: Stick erscheint, Systemplatte nicht; interne Platten erst nach Checkbox.
3. Explorer-Fenster auf Stick offen → Fehlermeldung "in Benutzung".
4. Vollständiger Lauf mit Standard-Durchgängen → grüner Status.
5. HxD → "Datenträger öffnen" → physischer Datenträger: Suche nach Nicht-Null-Byte findet nichts; Anfang/Ende stichprobenartig prüfen.
6. PhotoRec auf den physischen Datenträger: 0 wiederhergestellte Dateien.
7. "Prüfen"-Button separat → Erfolg.
8. Abbrechen während Lauf → Status abgebrochen, anschließend "Prüfen" → Fehler (erwartet).

Das Vorgehen wird als `docs/manual-test.md` mitgeliefert.

## Nicht im Umfang

- CLI-Modus, Linux-Laufzeitversion, ATA/NVMe Secure Erase, Löschen einzelner Dateien/Partitionen, Protokoll-Export (PDF/Zertifikat).
