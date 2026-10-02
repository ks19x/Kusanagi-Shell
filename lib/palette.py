#!/usr/bin/env python3
"""Kusanagi colours from a wallpaper (OKLCH clustering via ImageMagick).

usage: palette.py <image> [out.json]     default out: ~/.config/kusanagi/colors.json

Picks the most characterful colour as the accent, a second hue for accent2, and builds dark
surfaces / text around them. Greyscale wallpapers get a near-neutral UI. Written in place, so
Kusanagi (which watches the file) re-themes live.
"""
import json
import math
import os
import re
import subprocess
import sys


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def hex_to_oklch(h):
    r, g, b = (srgb_to_linear(int(h[i:i + 2], 16) / 255) for i in (0, 2, 4))
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s
    a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return L, math.hypot(a, bb), math.degrees(math.atan2(bb, a)) % 360


def oklch(L, C, H):
    """OKLCH -> '#rrggbb', pulling chroma in until it fits sRGB."""
    while True:
        a, b = C * math.cos(math.radians(H)), C * math.sin(math.radians(H))
        l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
        m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
        s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
        rgb = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
               -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
               -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)
        if all(-1e-4 <= c <= 1 + 1e-4 for c in rgb) or C < 0.001:
            break
        C *= 0.95
    return "#" + "".join(f"{round(max(0, min(1, linear_to_srgb(max(0, c)))) * 255):02x}" for c in rgb)


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def hue_dist(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


def clusters(img):
    out = subprocess.run(["magick", f"{img}[0]", "-resize", "256x256>", "-colors", "16", "-depth", "8",
                          "-format", "%c", "histogram:info:-"], capture_output=True, text=True, check=True).stdout
    found = [(int(n), h) for n, h in re.findall(r"^\s*(\d+):.*?#([0-9A-Fa-f]{6})", out, re.M)]
    total = sum(n for n, _ in found) or 1
    return [(n / total, *hex_to_oklch(h)) for n, h in found]


def palette(img):
    cl = clusters(img)

    def score(c):
        share, L, C, _ = c
        return C * share ** 0.35 * (1 if 0.25 < L < 0.92 else 0.3)

    accent = max(cl, key=score)
    mono = accent[2] < 0.035
    H = accent[3]
    C = 0.02 if mono else clamp(accent[2], 0.06, 0.15)
    others = [c for c in cl if hue_dist(c[3], H) > 35 and c[2] > 0.04]
    if others:
        best = max(others, key=score)
        H2, C2 = best[3], clamp(best[2], 0.05, 0.12)
    else:
        H2, C2 = (H + 40) % 360, C * 0.8
    if mono:
        C2 = 0.015
    surface = oklch(0.23, min(C * 0.25, 0.025), H)
    overlay = oklch(0.34, min(C * 0.3, 0.03), H)
    return {
        "text": oklch(0.94, 0.012, H), "textDim": oklch(0.68, 0.03, H), "danger": oklch(0.70, 0.17, 25),
        "accent": oklch(0.80, C, H), "accent2": oklch(0.78, C2, H2), "border": surface,
        "bgPanel": oklch(0.16, min(C * 0.2, 0.02), H), "bgCard": surface, "borderAccent": overlay,
        "textFaint": overlay, "ok": oklch(0.78, 0.12, 145), "trackBg": surface,
    }


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser("~/.config/kusanagi/colors.json")
    data = json.dumps(palette(sys.argv[1]), indent=2) + "\n"
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:          # in place: Kusanagi's file watcher reacts to it
        f.write(data)


if __name__ == "__main__":
    main()
