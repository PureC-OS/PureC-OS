#!/usr/bin/env python3
"""Rasterize assets/icons/*.svg into alpha-mask .icn files for the desktop.

.icn layout: magic 'PCIC', u16 width, u16 height (little endian), then
width*height bytes of 8-bit coverage. The desktop tints the mask with the
theme colour at draw time. Needs rsvg-convert and Pillow.
"""
import io
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image

SIZE = 96


def convert(svg: Path, out: Path) -> None:
    png = subprocess.run(
        ["rsvg-convert", "-w", str(SIZE), "-h", str(SIZE), str(svg)],
        check=True, capture_output=True).stdout
    alpha = Image.open(io.BytesIO(png)).convert("RGBA").getchannel("A")
    out.write_bytes(b"PCIC" + struct.pack("<HH", SIZE, SIZE) + alpha.tobytes())


def main() -> int:
    root = Path(__file__).resolve().parent.parent / "assets" / "icons"
    for svg in sorted(root.glob("*.svg")):
        convert(svg, svg.with_suffix(".icn"))
        print(f"{svg.name} -> {svg.with_suffix('.icn').name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
