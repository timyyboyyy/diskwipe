# diskwipe

Minimalistisches Windows-Tool zum vollständigen, verifizierten Überschreiben von Datenträgern – vor allem USB-Sticks.

- Rohzugriff auf das physische Laufwerk – das Dateisystem (FAT32, exFAT, NTFS …) wird komplett mit überschrieben
- Standard: **3 Zufallsdurchgänge + 1 Nulldurchgang**, jeder Durchgang wird vollständig zurückgelesen und verifiziert
- **Prüfen**-Funktion: bestätigt für jeden Datenträger, dass jedes Byte `0x00` ist
- Die Laufwerksliste zeigt je Eintrag `Disk 3 – E: (MEINSTICK), F: – Modell – 14,6 GB – USB`; vor dem Löschen nennt der Bestätigungsdialog alle Partitionen des Laufwerks
- Die Laufwerksliste aktualisiert sich beim Ein- und Abstecken automatisch; der Gesamtfortschritt steht im Balken und auf dem Taskleisten-Symbol
- Schutz: standardmäßig nur USB-/Wechseldatenträger, die Systemplatte (inkl. Boot-/EFI-Platte) nie, Bestätigung durch Eintippen von `LÖSCHEN`

## Installation

1. Unter [Releases](https://github.com/timyyboyyy/diskwipe/releases) die neueste `diskwipe-<version>-setup.exe` herunterladen (oder die `-portable.exe` ohne Installation).
2. Optional Prüfsumme kontrollieren: `Get-FileHash .\diskwipe-<version>-setup.exe` und mit `SHA256SUMS.txt` vergleichen.
3. Installer starten. Die Dateien sind nicht signiert – SmartScreen: **Weitere Informationen → Trotzdem ausführen**.
4. diskwipe über das Startmenü starten (fordert Administratorrechte an).

Deinstallation über *Einstellungen → Apps → diskwipe*.

## Benutzung

Stick einstecken → in diskwipe auswählen → **Löschen** → `LÖSCHEN` eintippen. Nach Abschluss zeigt ein grüner Status, dass der gesamte Datenträger verifiziert nur noch Nullen enthält. Danach ist der Stick unformatiert; zur Weiterverwendung in der Datenträgerverwaltung neu partitionieren.

**Achtung:** Gelöschte Daten sind unwiederbringlich verloren.

## Grenzen

Flash-Speicher (USB-Sticks, SSDs) nutzt Wear-Leveling und Reserveblöcke, die per Software nicht adressierbar sind. diskwipe überschreibt und verifiziert den gesamten vom Betriebssystem sichtbaren Bereich; mehrere Zufallsdurchgänge erhöhen die Chance, dass auch rotierte Blöcke erfasst werden. Gegen Labor-Forensik (Chip-Off) hilft nur zusätzlich die physische Zerstörung.

## Entwicklung (WSL/Linux)

```sh
sudo apt install g++ make mingw-w64 nsis testdisk mtools dosfstools python3
make test       # Linux: Unit-Tests + FAT32-Integrationstest mit grep, cmp und PhotoRec
make test-win   # Unit-Tests als Windows-Exe über WSL-Interop
make windows    # build/diskwipe.exe
make dist       # dist/: Installer, portable Exe, SHA256SUMS.txt
```

Release: Version in `VERSION` erhöhen, committen, `git tag v<version> && git push --tags` – GitHub Actions baut, testet (Linux + Windows) und veröffentlicht das Release.

Manuelle Testanleitung: [`docs/manual-test.md`](docs/manual-test.md) · Design: [`docs/superpowers/specs/2026-10-04-diskwipe-design.md`](docs/superpowers/specs/2026-10-04-diskwipe-design.md)
