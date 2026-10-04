#!/usr/bin/env python3
"""Erzeugt Testdateien mit eindeutigen Markern für den Integrationstest."""
import os
import random
import struct
import sys
import zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rng = random.Random(1234)

for i in range(3):
    with open(os.path.join(out, f"geheim_{i}.txt"), "w") as f:
        for line in range(5000):
            f.write(f"DISKWIPE_MARKER_{i}_{line} Vertrauliche Zeile, Konto DE{rng.randrange(10**20):020d}\n")

with open(os.path.join(out, "geheim_geloescht.txt"), "w") as f:
    for line in range(2000):
        f.write(f"DISKWIPE_MARKER_DELETED_{line} diese Datei wird vor dem Test geloescht\n")


def png(path, w, h):
    raw = b"".join(b"\x00" + bytes(rng.randrange(256) for _ in range(w * 3)) for _ in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw)))
        f.write(chunk(b"IEND", b""))


def pdf(path):
    stream = b"BT /F1 12 Tf 72 720 Td (DISKWIPE_MARKER_PDF) Tj ET\n" + b"% Fuelltext fuer Groesse\n" * 4000
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R >>",
        b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream",
    ]
    body = b"%PDF-1.4\n"
    offsets = []
    for n, obj in enumerate(objects, start=1):
        offsets.append(len(body))
        body += b"%d 0 obj\n" % n + obj + b"\nendobj\n"
    xref = len(body)
    body += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    body += b"".join(b"%010d 00000 n \n" % o for o in offsets)
    body += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
    with open(path, "wb") as f:
        f.write(body)


png(os.path.join(out, "bild.png"), 200, 200)
pdf(os.path.join(out, "dokument.pdf"))
