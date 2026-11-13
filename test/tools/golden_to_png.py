#!/usr/bin/env python3
"""Render test/golden/*.txt framebuffer snapshots as PNG screenshots.

Usage: python3 test/tools/golden_to_png.py [out_dir]   (default: docs/images)
Needs Pillow. '#' is a lit pixel, '.' a dark one.
"""
import pathlib
import sys

from PIL import Image

SCALE = 4          # output pixels per OLED pixel
GAP = 0            # dark gap between pixels (1 gives a panel look, hurts inverted text)
BORDER = 12        # bezel around the active area
LIT = (210, 235, 255)
DARK = (10, 12, 16)
BEZEL = (0, 0, 0)

root = pathlib.Path(__file__).resolve().parents[2]
out_dir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "docs" / "images"
out_dir.mkdir(parents=True, exist_ok=True)

for golden in sorted((root / "test" / "golden").glob("*.txt")):
    rows = [r for r in golden.read_text().splitlines() if r]
    h, w = len(rows), len(rows[0])
    img = Image.new("RGB", (w * SCALE + 2 * BORDER, h * SCALE + 2 * BORDER), BEZEL)
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            colour = LIT if ch == "#" else DARK
            for dy in range(SCALE - GAP):
                for dx in range(SCALE - GAP):
                    px[BORDER + x * SCALE + dx, BORDER + y * SCALE + dy] = colour
    target = out_dir / (golden.stem + ".png")
    img.save(target, optimize=True)
    print(target.relative_to(root) if target.is_relative_to(root) else target)
