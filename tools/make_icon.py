#!/usr/bin/env python3
"""Erzeugt src/diskwipe.ico (und tools/icon-preview.png) mit Pillow."""
from pathlib import Path
from PIL import Image, ImageDraw

S = 1024
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
ROOT = Path(__file__).resolve().parent.parent


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def background():
    c1, c2 = (0x1E, 0x3A, 0x5F), (0x0F, 0x76, 0x6E)
    grad = Image.new("RGB", (S, S))
    px = grad.load()
    for y in range(S):
        for x in range(S):
            px[x, y] = lerp(c1, c2, (x + y) / (2 * (S - 1)))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((16, 16, S - 17, S - 17), radius=220, fill=255)
    img = grad.convert("RGBA")
    img.putalpha(mask)
    return img


OUTLINE = (0x10, 0x24, 0x3E, 255)


def usb_stick(small=False):
    """Stick aufrecht zeichnen und drehen. small: chunkier, dicke Kontur, 45 Grad, ohne Details."""
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    white, metal, dark = (255, 255, 255, 255), (0xCB, 0xD5, 0xE1, 255), (0x1E, 0x3A, 0x5F, 255)
    cx = S // 2
    if small:
        o = 60
        conn, body = (cx - 140, 40, cx + 140, 440), (cx - 215, 400, cx + 215, 930)
        d.rounded_rectangle(tuple(v + (-o, -o, o, o)[i] for i, v in enumerate(conn)), radius=50, fill=OUTLINE)
        d.rounded_rectangle(tuple(v + (-o, -o, o, o)[i] for i, v in enumerate(body)), radius=130, fill=OUTLINE)
        d.rounded_rectangle(conn, radius=20, fill=metal)
        d.rounded_rectangle(body, radius=80, fill=white)
        angle = 45
    else:
        o = 22
        d.rounded_rectangle((cx - 120 - o, 90 - o, cx + 120 + o, 440), radius=40, fill=OUTLINE)
        d.rounded_rectangle((cx - 190 - o, 400 - o, cx + 190 + o, 960 + o), radius=100, fill=OUTLINE)
        d.rounded_rectangle((cx - 120, 90, cx + 120, 440), radius=24, fill=metal)
        d.rectangle((cx - 70, 150, cx - 20, 230), fill=dark)
        d.rectangle((cx + 20, 150, cx + 70, 230), fill=dark)
        d.rounded_rectangle((cx - 190, 400, cx + 190, 960), radius=80, fill=white)
        d.rectangle((cx - 190, 470, cx + 190, 520), fill=metal)
        angle = 35
    layer = layer.rotate(angle, resample=Image.BICUBIC, center=(cx, S // 2))
    if small:
        layer = ImageChops_offset(layer, -40, -40)
    return layer


def ImageChops_offset(img, dx, dy):
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (dx, dy))
    return out


def badge(small=False):
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy, r = (740, 740, 215) if small else (700, 720, 250)
    d.ellipse((cx - r - 36, cy - r - 36, cx + r + 36, cy + r + 36), fill=(0x14, 0x2B, 0x47, 255))
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(0xDC, 0x26, 0x26, 255))
    k, w = (88, 62) if small else (105, 62)
    d.line((cx - k, cy - k, cx + k, cy + k), fill="white", width=w)
    d.line((cx - k, cy + k, cx + k, cy - k), fill="white", width=w)
    for sx, sy in ((-k, -k), (k, k), (-k, k), (k, -k)):
        d.ellipse((cx + sx - w // 2, cy + sy - w // 2, cx + sx + w // 2, cy + sy + w // 2), fill="white")
    return layer




def render(small=False):
    img = background()
    img.alpha_composite(usb_stick(small))
    img.alpha_composite(badge(small))
    return img


def frame(master, small_master, size):
    src = small_master if size <= 24 else master
    big = src.resize((size * 8, size * 8), Image.LANCZOS)  # 8x, dann Box-Downscale
    return big.resize((size, size), Image.BOX if size <= 24 else Image.LANCZOS)


def write_ico(path, frames):
    """ICO-Container mit PNG-Frames (Pillow schreibt sonst aus einem Master)."""
    import io, struct
    blobs = []
    for im in frames:
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        blobs.append(buf.getvalue())
    out = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    off = 6 + 16 * len(frames)
    for im, blob in zip(frames, blobs):
        w = 0 if im.width >= 256 else im.width
        out += struct.pack("<BBBBHHII", w, w, 0, 0, 1, 32, len(blob), off)
        off += len(blob)
    for blob in blobs:
        out += blob
    Path(path).write_bytes(bytes(out))


def main():
    master, small_master = render(), render(True)
    frames = [frame(master, small_master, s) for s in SIZES]
    ico = ROOT / "src" / "diskwipe.ico"
    write_ico(ico, frames)
    frames[-1].save(ROOT / "tools" / "icon-preview.png")
    # Kontrollbild aus der geschriebenen ICO-Datei (16/20/24/32 vergroessert)
    check = Image.open(ico)
    strip = Image.new("RGBA", (16 * 8 + 20 * 6 + 24 * 5 + 32 * 4 + 50, 140), (255, 255, 255, 255))
    x = 10
    for s, z in ((16, 8), (20, 6), (24, 5), (32, 4)):
        check.size = (s, s)
        t = check.convert("RGBA")
        t = t.resize((s * z, s * z), Image.NEAREST)
        strip.alpha_composite(t, (x, 5))
        x += s * z + 10
    strip.save(ROOT / "tools" / "icon-small-preview.png")
    print("geschrieben:", ico, sorted(check.info["sizes"]))


if __name__ == "__main__":
    main()
