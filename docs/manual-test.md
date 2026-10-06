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
7. Laufwerk auswählen → „Löschen" klicken → solange der LÖSCHEN-Bestätigungsdialog offen ist, den Stick abziehen und einen anderen einstecken (anderes Modell, andere Größe oder andere Seriennummer; baugleiche Sticks ohne Seriennummer sind nicht unterscheidbar) → LÖSCHEN eintippen und bestätigen (der Dialog nennt den gewählten Eintrag und die Zeile „Alle Partitionen auf diesem Laufwerk werden gelöscht: E: (DATEN), F: (BACKUP)", ohne Laufwerksbuchstaben „… (keine mit Laufwerksbuchstaben).") → Erwartete Meldung „Laufwerk hat sich geändert – bitte Liste aktualisieren", nichts wird geschrieben. Danach aktualisiert sich die Liste automatisch.

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
17. Fenster offen lassen, Stick abziehen und wieder einstecken, ohne „Aktualisieren" zu drücken → die Liste aktualisiert sich nach ca. einer halben Sekunde, im Log erscheinen „Laufwerk entfernt: …" / „Laufwerk angeschlossen: …". Ist ein Stick ausgewählt und ein anderer Stick wird ein-/abgesteckt, bleibt die Auswahl erhalten. Wird der ausgewählte Stick abgezogen und es bleiben andere Laufwerke übrig, wird die Auswahl geleert (Löschen/Prüfen ausgegraut, Log „Ausgewähltes Laufwerk entfernt – bitte neu auswählen."); es wird nie automatisch ein anderes Laufwerk gewählt. Automatisch ausgewählt wird nur, wenn die Liste vorher leer war (z.B. einziger Stick wird eingesteckt); in allen anderen Fällen ohne Auswahl bleibt die Auswahl leer, bis man selbst wählt.
18. Jeder Eintrag hat das Format `Disk 3 – E: (MEINSTICK), F: – Modell – 14,6 GB – USB` (Disk – Laufwerksbuchstaben mit Bezeichnung – Modell – Größe – USB/Wechseldatenträger/Intern); ein Stick ohne Bezeichnung zeigt nur `E:`, ohne Laufwerksbuchstaben steht `-`. Die aufgeklappte Liste ist breit genug, dass kein Eintrag abgeschnitten wird.
19. Icon (USB-Stick mit Lösch-Symbol) ist sichtbar im Fenster, in der Taskleiste, an die Taskleiste angeheftet, im Startmenü und im Installer/Deinstaller.
20. Während eines Laufs steht im grünen Balken zentriert `Gesamt: N %` (lesbar auf Grün und Grau, auch bei 0 % und 100 %); die Statuszeile darunter zeigt weiter Durchgang, Schritt-Prozent, MB/s und Restzeit. Balken ca. 10 s beobachten, während sich die Prozentzahl nicht ändert: Text bleibt sichtbar, kein Flackern.
21. Das Taskleisten-Symbol zeigt während des Laufs grünen Fortschritt, nach „Abbrechen" gelb, nach einem Fehler oder fehlgeschlagener Prüfung rot (bis zum nächsten Start); nach erfolgreichem Ende keine Anzeige.

## Fortsetzen nach Abstecken und Protokoll

22. Löschen mit 3 Zufallsdurchgängen starten. In Durchgang 2 beim Schreiben den Stick abziehen → oranger Status „Unterbrochen: …", Statuszeile „Unterbrochen – Durchgang 2/4, Schreiben bei N % – Laufwerk wieder einstecken", Taskleiste gelb, „Fortsetzen" ausgegraut, „Abbrechen" aktiv. Kein Speichern-Dialog.
23. Stick an einem **anderen** USB-Port wieder einstecken → nach ca. einer halben Sekunde wird er ausgewählt, Log „Laufwerk des unterbrochenen Vorgangs erkannt: …", Status zeigt „… – Laufwerk wieder erkannt – Fortsetzen klicken", „Fortsetzen" aktiv. Klicken → Lauf geht ab ungefähr derselben Prozentzahl weiter (höchstens 64 MiB zurück), ohne LÖSCHEN-Abfrage. Ende grün, danach „Speichern unter" mit Vorschlag `diskwipe_<Datum>_<Modell>_SN<…>.log` im Ordner Dokumente.
24. Gespeichertes Protokoll im Editor öffnen: Kopf mit Version, Vorgang und Laufwerk (mit Seriennummer); je Phase Start/Ende mit Zeitstempel; „Unterbrechung in Durchgang 2/4 (Schreiben): …"; „Laufwerk wieder erkannt: Disk …"; „Inhaltsprüfung: passt …"; „Fortgesetzt in Durchgang 2/4 …"; letzte Zeile „Ergebnis: Erfolg …". Keine Seeds.
25. Wie 22, dann statt des Sticks einen **anderen** Stick gleichen Modells einstecken (falls vorhanden; sonst Schritt überspringen): Hat er eine andere Seriennummer, bleibt „Fortsetzen" grau. Hat er dieselbe Seriennummer (Billig-Klon), wird beim Klick auf „Fortsetzen" „Inhalt passt nicht zum unterbrochenen Vorgang – falscher Stick?" gemeldet (Meldung in Ergebnisfeld und Statuszeile); mit HxD prüfen, dass dieser Stick unverändert ist.
26. Wie 22, beide Sticks gleichzeitig stecken (nur bei gleicher Seriennummer relevant) → Statuszeile „mehrere passende Laufwerke, das andere abziehen".
27. Wie 22, dann „Abbrechen" → Rückfrage „Verwerfen?"; „Nein" → Stand bleibt; „Ja" → oranger Status „Verworfen – …", Speichern-Dialog, Protokoll endet mit „Ergebnis: Vorgang verworfen – Datenträger unvollständig gelöscht". Ebenso: bei offenem Stand „Löschen" oder „Prüfen" klicken → dieselbe Rückfrage; Fenster schließen → Rückfrage „Trotzdem beenden?".
28. „Prüfen" starten und Stick abziehen → roter Fehler, **kein** Fortsetzen-Stand, Speichern-Dialog erscheint.
29. Stick in der Prüfphase eines Löschdurchgangs abziehen, wieder einstecken, „Fortsetzen" → Prüfphase läuft ab dem unterbrochenen Block weiter, Ende grün.
30. Speichern-Dialog mit „Abbrechen" schließen → Log-Feld zeigt „Protokoll liegt unter: …\diskwipe-….log"; die Datei existiert in `%TEMP%`.
