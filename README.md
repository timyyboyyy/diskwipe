# diskwipe

Minimalistisches Windows-Tool zum vollständigen, verifizierten Überschreiben von Datenträgern (primär USB-Sticks).

- Rohzugriff auf `\\.\PhysicalDriveN` – Dateisystem (FAT32, exFAT, NTFS …) wird komplett mit überschrieben
- Standard: 3 Zufallsdurchgänge + 1 Nulldurchgang, jeder Durchgang wird vollständig zurückgelesen und verifiziert
- Separater „Prüfen“-Modus: bestätigt, dass jedes Byte 0x00 ist
- Schutz: nur USB-/Wechseldatenträger standardmäßig, Systemplatte nie, Bestätigung per Eingabe von `LÖSCHEN`

> **Status:** in Entwicklung. Design: [`docs/superpowers/specs/2026-10-04-diskwipe-design.md`](docs/superpowers/specs/2026-10-04-diskwipe-design.md)

## Grenzen

Flash-Speicher (USB-Sticks, SSDs) nutzt Wear-Leveling und Reserveblöcke, die per Software nicht adressierbar sind. diskwipe überschreibt und verifiziert den gesamten vom Betriebssystem sichtbaren Bereich. Gegen Labor-Forensik (Chip-Off) hilft nur zusätzlich die physische Zerstörung.

## Build (WSL/Linux)

```sh
sudo apt install mingw-w64 testdisk mtools dosfstools
make windows   # build/diskwipe.exe
make test      # Linux-Tests (inkl. PhotoRec-Integrationstest)
make test-win  # Tests als Windows-Exe über WSL-Interop
```

## Benutzung

`diskwipe.exe` unter Windows starten (fordert Adminrechte an), Laufwerk wählen, „Löschen“.

**Achtung:** Gelöschte Daten sind unwiederbringlich verloren.
