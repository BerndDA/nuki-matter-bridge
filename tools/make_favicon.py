#!/usr/bin/env python3
"""Generate main/web/favicon.ico: the web UI's lock glyph, white on the accent blue.

Pure Python (no Pillow): each pixel is supersampled against simple shapes, the
sizes are stored as PNG images inside the .ico (supported by all current browsers).

    python3 tools/make_favicon.py            # writes main/web/favicon.ico
"""
import math
import os
import struct
import sys
import zlib

ACCENT = (0x25, 0x63, 0xEB)
WHITE = (0xFF, 0xFF, 0xFF)
SIZES = (16, 32, 48)
SS = 8  # supersampling per axis


def in_round_rect(x, y, x0, y0, x1, y1, r):
    if not (x0 <= x <= x1 and y0 <= y <= y1):
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def background(x, y):
    """Rounded square filling the icon (unit coordinates 0..24)."""
    return in_round_rect(x, y, 0, 0, 24, 24, 5.5)


def glyph(x, y):
    """Lock: solid body with a keyhole, and a shackle (stroked arc + legs)."""
    body = in_round_rect(x, y, 5, 11, 19, 21, 2.2)
    keyhole = (x - 12) ** 2 + (y - 15.2) ** 2 <= 1.7 ** 2 or (11.2 <= x <= 12.8 and 15.2 <= y <= 18.2)
    if body and not keyhole:
        return True
    # shackle: half ring centred at (12, 8), radius 4, stroke 2.4, legs down to the body
    half = 1.2
    if y <= 8:
        d = math.hypot(x - 12, y - 8)
        return abs(d - 4) <= half
    return 8 <= y <= 11.5 and (abs(x - 8) <= half or abs(x - 16) <= half)


def render(size):
    rows = []
    for py in range(size):
        row = bytearray([0])  # PNG filter type: none
        for px in range(size):
            bg = fg = 0
            for sy in range(SS):
                for sx in range(SS):
                    x = (px + (sx + 0.5) / SS) * 24 / size
                    y = (py + (sy + 0.5) / SS) * 24 / size
                    if background(x, y):
                        bg += 1
                        if glyph(x, y):
                            fg += 1
            n = SS * SS
            if bg == 0:
                row += bytes(4)
                continue
            t = fg / bg
            rgb = [round(a + (b - a) * t) for a, b in zip(ACCENT, WHITE)]
            row += bytes(rgb + [round(255 * bg / n)])
        rows.append(bytes(row))
    return png(size, b"".join(rows))


def png(size, raw):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def ico(images):
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for size, img in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(img), offset + len(data))
        data += img
    return header + entries + data


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "main", "web", "favicon.ico")
    blob = ico([(s, render(s)) for s in SIZES])
    with open(out, "wb") as f:
        f.write(blob)
    print(f"wrote {out} ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
