# Manueller Test unter Windows

Benötigt: ein USB-Stick **ohne wichtige Daten**, [HxD](https://mh-nexus.de/de/hxd/), [PhotoRec](https://www.cgsecurity.org/wiki/PhotoRec) (TestDisk-Paket für Windows).

## Vorbereitung
1. Stick in Windows formatieren (FAT32) und ein paar Dateien darauf kopieren (Fotos, PDFs, Textdateien). Eine davon wieder löschen.
2. PhotoRec (`photorec_win.exe`) als Admin starten, den Stick wählen, *Whole* durchsuchen → es müssen Dateien wiederhergestellt werden. Ergebnis notieren.

## Oberfläche
3. `diskwipe` aus dem Startmenü starten → UAC-Abfrage erscheint.
4. Laufwerksliste: Der Stick erscheint, die Systemplatte (C:) **nicht**. Interne Platten erscheinen erst nach der Checkbox und deren Warnung. Geräte unter 1 MiB (z.B. LCD-/Peripherie-Firmware) erscheinen nicht.
5. Explorer-Fenster auf dem Stick offen lassen, eine Datei vom Stick in einem Editor öffnen → „Löschen" → bestätigen → erwartete Meldung „Laufwerk in Benutzung …", es wird nichts geschrieben. Editor/Explorer schließen.
6. Bestätigungsdialog: „loeschen" oder „Löschen" eintippen → Button bleibt grau; Enter drücken → Dialog bleibt offen. Erst exakt `LÖSCHEN` aktiviert den Button.

## Validierung der Laufwerksintegrität
7. Laufwerk auswählen, Stick abziehen und einen anderen einstecken, ohne 'Aktualisieren' → Löschen → bestätigen → Es muss ein Stick mit anderem Modell, anderer Größe oder anderer Seriennummer sein; baugleiche Sticks ohne Seriennummer sind nicht unterscheidbar. Erwartete Meldung 'Laufwerk hat sich geändert – bitte Liste aktualisieren', nichts wird geschrieben.

## Löschen
8. Vollständigen Lauf mit 3 Zufallsdurchgängen starten. Fortschritt, MB/s und Restzeit werden angezeigt. Ende: grüner Status „Erfolg: alle N Bytes = 0x00 (4 Durchgänge …)" und Hinweisdialog.
9. „Prüfen" separat ausführen → grüner Status.

## Gegenprobe mit fremden Tools
10. HxD als Admin → *Extras → Datenträger öffnen* → den physischen Datenträger des Sticks wählen, *Schreibgeschützt öffnen* aktiviert lassen.
    - Anfang und Ende ansehen: nur `00`.
    - *Analyse → Statistik*: die Byte-Verteilung zeigt ausschließlich `00` (100 %).
    - Zusätzlich *Suchen* (Hex) nach `FFD8FF` (JPEG-Kopf) und `25504446` (`%PDF`): keine Treffer.
11. PhotoRec erneut auf den Stick (*Whole*) → **0 Dateien**.

## Abbruch
12. Neuen Lauf starten, nach einigen Sekunden „Abbrechen" → oranger Status „Abgebrochen – Datenträger unvollständig gelöscht". Danach „Prüfen" → roter Status mit abweichenden Bytes (erwartet).
13. Lauf starten und Fenster schließen → Rückfrage; „Ja" → Programm beendet sich nach dem aktuellen Block.

## Installer
14. Installer ausführen: Startmenü-Eintrag vorhanden, optional Desktop-Verknüpfung, Eintrag unter *Einstellungen → Apps* mit Version.
15. Deinstallieren über *Apps* → Programmordner, Startmenü-Eintrag und Verknüpfung sind weg.
16. Portable Exe direkt starten → funktioniert identisch.

## Automatische Liste, Icon und Gesamtfortschritt
17. Fenster offen lassen, Stick abziehen und wieder einstecken, ohne „Aktualisieren" zu drücken → die Liste aktualisiert sich nach ca. einer halben Sekunde, im Log erscheinen „Laufwerk entfernt: …" / „Laufwerk angeschlossen: …". Ist ein Stick ausgewählt und ein anderer Stick wird ein-/abgesteckt, bleibt die Auswahl erhalten.
18. Jeder Eintrag zeigt Laufwerksbuchstabe und Bezeichnung (z.B. `E: (MEINSTICK)`); ein Stick ohne Bezeichnung zeigt nur `E:`.
19. Icon (USB-Stick mit Lösch-Symbol) ist sichtbar im Fenster, in der Taskleiste, an die Taskleiste angeheftet, im Startmenü und im Installer/Deinstaller.
20. Während eines Laufs steht im grünen Balken zentriert `Gesamt: N %` (lesbar auf Grün und Grau, auch bei 0 % und 100 %); die Statuszeile darunter zeigt weiter Durchgang, Schritt-Prozent, MB/s und Restzeit. Balken ca. 10 s beobachten, während sich die Prozentzahl nicht ändert: Text bleibt sichtbar, kein Flackern.
21. Das Taskleisten-Symbol zeigt während des Laufs grünen Fortschritt, nach „Abbrechen" gelb, nach einem Fehler oder fehlgeschlagener Prüfung rot (bis zum nächsten Start); nach erfolgreichem Ende keine Anzeige.
