## Neu in 1.1.0

- **Fortsetzen nach Abstecken:** Wird der Stick während des Löschens abgezogen, läuft der Vorgang nach dem Wiedereinstecken mit **Fortsetzen** an der unterbrochenen Stelle weiter (höchstens 64 MiB zurück), solange diskwipe geöffnet bleibt. Der Stick wird über Modell, Seriennummer und Größe wiedererkannt und vor dem ersten Schreiben über seinen Inhalt geprüft – ein anderer Stick wird nicht beschrieben.
- **Ausführliches Protokoll:** Jeder Lauf protokolliert mit Zeitstempeln:
  - System und Laufwerk, Partitionstabelle vor dem Löschen
  - Sperren und Aushängen der Volumes
  - Fortschritt, Checkpoints, Dauer und MB/s je Durchgang
  - Unterbrechungen, Windows-Fehlercodes und Ergebnis
- **Protokoll speichern:** Gespeichert wird über den neuen Button **Protokoll speichern**; vor einem neuen Lauf oder beim Beenden fragt diskwipe nach, falls das Protokoll noch nicht gespeichert ist.
- Versionsinfo der Exe zeigt Umlaute korrekt an.

## Download

- **diskwipe-…-setup.exe** – Installer (Startmenü, Deinstaller unter „Apps & Features“)
- **diskwipe-…-portable.exe** – ohne Installation starten
- **SHA256SUMS.txt** – Prüfsummen

Prüfsumme in PowerShell kontrollieren:

    Get-FileHash .\diskwipe-<version>-setup.exe -Algorithm SHA256

Die Dateien sind nicht signiert. Windows SmartScreen zeigt beim ersten Start „Der Computer wurde durch Windows geschützt“ → **Weitere Informationen** → **Trotzdem ausführen**.

**Achtung:** diskwipe überschreibt den gewählten Datenträger vollständig. Gelöschte Daten sind unwiederbringlich verloren.
