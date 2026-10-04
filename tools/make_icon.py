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


def usb_stick():
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    white, metal, dark = (255, 255, 255, 255), (0xCB, 0xD5, 0xE1, 255), (0x1E, 0x3A, 0x5F, 255)
    cx = S // 2
    # Stecker (oben), metallgrau mit zwei dunklen Kontaktloechern
    d.rounded_rectangle((cx - 120, 90, cx + 120, 440), radius=24, fill=metal)
    d.rectangle((cx - 70, 150, cx - 20, 230), fill=dark)
    d.rectangle((cx + 20, 150, cx + 70, 230), fill=dark)
    # Gehaeuse (weiss)
    d.rounded_rectangle((cx - 190, 400, cx + 190, 960), radius=80, fill=white)
    # Ose / Akzentstreifen
    d.rounded_rectangle((cx - 190, 470, cx + 190, 520), radius=0, fill=metal)
    layer = layer.rotate(35, resample=Image.BICUBIC, center=(cx, S // 2))
    return layer.resize((S, S), Image.LANCZOS)


def badge():
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy, r = 700, 720, 250
    d.ellipse((cx - r - 36, cy - r - 36, cx + r + 36, cy + r + 36), fill=(0x14, 0x2B, 0x47, 255))
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(0xDC, 0x26, 0x26, 255))
    k, w = 105, 62
    d.line((cx - k, cy - k, cx + k, cy + k), fill="white", width=w)
    d.line((cx - k, cy + k, cx + k, cy - k), fill="white", width=w)
    for sx, sy in ((-k, -k), (k, k), (-k, k), (k, -k)):
        d.ellipse((cx + sx - w // 2, cy + sy - w // 2, cx + sx + w // 2, cy + sy + w // 2), fill="white")
    return layer


def render():
    img = background()
    img.alpha_composite(usb_stick())
    img.alpha_composite(badge())
    return img


def main():
    img = render()
    ico = ROOT / "src" / "diskwipe.ico"
    img.save(ico, format="ICO", sizes=[(s, s) for s in SIZES])
    img.resize((256, 256), Image.LANCZOS).save(ROOT / "tools" / "icon-preview.png")
    # Kontrollbild: kleine Groessen vergroessert (nearest) nebeneinander
    strip = Image.new("RGBA", (16 * 8 + 20 * 8 + 32 * 4 + 40, 256), (255, 255, 255, 255))
    x = 10
    for s, z in ((16, 8), (20, 6), (32, 4)):
        t = img.resize((s, s), Image.LANCZOS).resize((s * z, s * z), Image.NEAREST)
        strip.alpha_composite(t, (x, 10))
        x += s * z + 10
    strip.save(ROOT / "tools" / "icon-small-preview.png")
    print("geschrieben:", ico)


if __name__ == "__main__":
    main()
