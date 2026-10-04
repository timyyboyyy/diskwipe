#!/usr/bin/env bash
# Integrationstest: FAT32-Image befüllen, löschen und mit grep, cmp und PhotoRec gegenprüfen.
set -euo pipefail
cd "$(dirname "$0")/.."
export MTOOLS_SKIP_CHECK=1

MKFS=$(command -v mkfs.vfat || echo /usr/sbin/mkfs.vfat)
for tool in "$MKFS" mcopy mdel photorec python3 cmp; do
    command -v "$tool" >/dev/null || { echo "FEHLT: $tool (sudo apt install dosfstools mtools testdisk python3)"; exit 1; }
done

TMP=tests/tmp/integration
IMG="$TMP/fat32.img"
SIZE=$((64 * 1024 * 1024 + 3 * 512))

fail() { echo "FAIL: $*"; exit 1; }

# Anzahl der von PhotoRec wiederhergestellten Dateien (gesamter Datenträger, alle Dateitypen).
recovered_count() {
    rm -rf "$1"
    mkdir -p "$1"
    photorec /log /d "$1/recup" /cmd "$IMG" partition_none,fileopt,everything,enable,wholespace,search \
        >"$1/photorec.out" 2>&1 </dev/null || true
    find "$1" -type f -path '*recup.*' ! -name report.xml | wc -l
}

rm -rf "$TMP"
mkdir -p "$TMP/files"

echo "== Image vorbereiten"
truncate -s "$SIZE" "$IMG"
"$MKFS" -F 32 -s 1 -n TESTSTICK "$IMG" >/dev/null
python3 tests/make_testfiles.py "$TMP/files"
mcopy -i "$IMG" "$TMP"/files/* ::/
mdel -i "$IMG" ::/geheim_geloescht.txt

echo "== Vorher-Prüfung"
grep -aq DISKWIPE_MARKER_0_ "$IMG" || fail "Marker vor dem Löschen nicht gefunden"
grep -aq DISKWIPE_MARKER_DELETED_ "$IMG" || fail "Marker der gelöschten Datei nicht gefunden"
BEFORE=$(recovered_count "$TMP/photorec_before")
echo "PhotoRec vorher: $BEFORE Dateien"
[ "$BEFORE" -gt 0 ] || fail "PhotoRec stellt vor dem Löschen nichts her – Aufbau prüfen: $TMP/photorec_before/photorec.out"

echo "== Löschen (3x Zufall + 1x Nullen)"
./build/wipe_image "$IMG" 3

echo "== Nachher-Prüfung"
[ "$(stat -c %s "$IMG")" -eq "$SIZE" ] || fail "Imagegröße verändert"
if grep -aq DISKWIPE_MARKER_ "$IMG"; then fail "Marker nach dem Löschen gefunden"; fi
cmp -n "$SIZE" "$IMG" /dev/zero || fail "Image enthält Nicht-Null-Bytes"
./build/wipe_image --verify "$IMG" || fail "verify meldet Fehler"
AFTER=$(recovered_count "$TMP/photorec_after")
echo "PhotoRec nachher: $AFTER Dateien"
[ "$AFTER" -eq 0 ] || fail "PhotoRec hat nach dem Löschen $AFTER Dateien gefunden"

echo "PASS integration"
