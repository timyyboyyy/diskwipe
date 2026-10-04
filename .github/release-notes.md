## Download

- **diskwipe-…-setup.exe** – Installer (Startmenü, Deinstaller unter „Apps & Features“)
- **diskwipe-…-portable.exe** – ohne Installation starten
- **SHA256SUMS.txt** – Prüfsummen

Prüfsumme in PowerShell kontrollieren:

    Get-FileHash .\diskwipe-<version>-setup.exe -Algorithm SHA256

Die Dateien sind nicht signiert. Windows SmartScreen zeigt beim ersten Start „Der Computer wurde durch Windows geschützt“ → **Weitere Informationen** → **Trotzdem ausführen**.

**Achtung:** diskwipe überschreibt den gewählten Datenträger vollständig. Gelöschte Daten sind unwiederbringlich verloren.
