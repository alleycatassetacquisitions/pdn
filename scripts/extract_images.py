#!/usr/bin/env python3
"""Extract XBM bitmaps from include/images-raw.hpp into PNGs.

Format: U8g2 drawXBMP: LSB-first within each byte, set bit = lit pixel.
Each entry in images-raw.hpp is preceded by a comment like
    // 'name', WxHpx
followed by
    const unsigned char image_name [] = { 0x.., 0x.., ... };
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC = REPO_ROOT / "include" / "images-raw.hpp"
OUT = REPO_ROOT / "images-extracted"
CODE_DIRS = (REPO_ROOT / "src", REPO_ROOT / "include")

HEADER_RE = re.compile(r"^//\s*'([^']+)',\s*(\d+)x(\d+)px\s*$")
ARRAY_RE = re.compile(r"^const\s+unsigned\s+char\s+image_(\w+)\s*\[\s*\]\s*=\s*\{")
BYTE_RE = re.compile(r"0x([0-9a-fA-F]{2})")
IMAGE_CTOR_RE = re.compile(r"Image\(\s*image_(\w+)\s*,\s*(\d+)\s*,\s*(\d+)")


def scan_code_dimensions() -> dict[str, tuple[int, int]]:
    """Scan src/ and include/ for Image(image_name, w, h, ...) and return name -> (w, h)."""
    found: dict[str, tuple[int, int]] = {}
    for root in CODE_DIRS:
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in (".cpp", ".hpp", ".h", ".cc"):
                continue
            if path.name == "images-raw.hpp":
                continue
            try:
                text = path.read_text(errors="ignore")
            except OSError:
                continue
            for m in IMAGE_CTOR_RE.finditer(text):
                name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
                found.setdefault(name, (w, h))
    return found


def parse(text: str, code_dims: dict[str, tuple[int, int]]):
    """Yield (name, width, height, bytes) for every image in the header.

    Dimensions resolved in this order: actual code usage, header comment,
    fallback inference from byte count. Yields width/height that match the
    actual byte count when possible.
    """
    lines = text.splitlines()
    metadata: dict[str, tuple[int, int]] = {}

    for line in lines:
        m = HEADER_RE.match(line)
        if m:
            name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
            metadata[name] = (w, h)
            continue

    i = 0
    while i < len(lines):
        line = lines[i]
        m = ARRAY_RE.match(line)
        if not m:
            i += 1
            continue
        var_name = m.group(1)
        body: list[str] = []
        j = i
        while j < len(lines):
            body.append(lines[j])
            if "};" in lines[j]:
                break
            j += 1
        joined = "\n".join(body)
        data = bytes(int(hx, 16) for hx in BYTE_RE.findall(joined))

        w, h = code_dims.get(var_name, metadata.get(var_name, (0, 0)))
        bytes_per_row = (w + 7) // 8 if w else 0
        if w == 0 or h == 0:
            print(f"warning: no dimensions for image_{var_name}", file=sys.stderr)
        elif bytes_per_row * h != len(data):
            # Header lied and code didn't override. Try header fallback explicitly.
            alt = metadata.get(var_name)
            if alt and ((alt[0] + 7) // 8) * alt[1] == len(data):
                w, h = alt
            else:
                print(
                    f"warning: image_{var_name} dims {w}x{h} expect "
                    f"{bytes_per_row * h} bytes, got {len(data)}",
                    file=sys.stderr,
                )
        yield var_name, w, h, data
        i = j + 1


def decode_xbm(data: bytes, width: int, height: int) -> list[list[int]]:
    """Return a 2D list of 0/1 pixels. LSB-first within byte, set=lit."""
    bytes_per_row = (width + 7) // 8
    if len(data) != bytes_per_row * height:
        raise ValueError(
            f"data length {len(data)} != expected {bytes_per_row * height}"
        )
    pixels: list[list[int]] = []
    for row in range(height):
        row_pixels: list[int] = []
        for col in range(width):
            byte = data[row * bytes_per_row + col // 8]
            bit = (byte >> (col % 8)) & 1
            row_pixels.append(bit)
        pixels.append(row_pixels)
    return pixels


def render(pixels: list[list[int]], lit_color: tuple[int, int, int], bg: tuple[int, int, int]) -> Image.Image:
    h = len(pixels)
    w = len(pixels[0]) if h else 0
    img = Image.new("RGB", (w, h), bg)
    px = img.load()
    for y, row in enumerate(pixels):
        for x, bit in enumerate(row):
            if bit:
                px[x, y] = lit_color
    return img


HTML_TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>PDN extracted pixel art ({count} images)</title>
<style>
  body {{ background: #1a1a1a; color: #eee; font-family: ui-monospace, monospace; padding: 24px; }}
  h1 {{ margin: 0 0 8px; font-size: 20px; }}
  .meta {{ color: #888; margin-bottom: 24px; }}
  .grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(290px, 1fr)); gap: 24px; }}
  figure {{ background: #222; padding: 12px; border-radius: 6px; margin: 0; }}
  figure img {{ image-rendering: pixelated; width: 256px; height: 128px; display: block; }}
  figure .pair {{ display: flex; gap: 8px; }}
  figure .pair > div {{ flex: 1; }}
  figure .pair img {{ width: 100%; height: auto; }}
  figcaption {{ margin-top: 8px; font-size: 12px; color: #ccc; word-break: break-all; }}
  .dims {{ color: #888; }}
</style>
</head>
<body>
<h1>PDN extracted pixel art</h1>
<div class="meta">{count} images from include/images-raw.hpp · left: white-on-black (OLED) · right: black-on-white (paper)</div>
<div class="grid">
{items}
</div>
</body>
</html>
"""

ITEM_TEMPLATE = """  <figure>
    <div class="pair">
      <div><img src="oled/{name}.png" alt="{name} (OLED)"></div>
      <div><img src="paper/{name}.png" alt="{name} (paper)"></div>
    </div>
    <figcaption>{name} <span class="dims">{w}×{h}</span></figcaption>
  </figure>"""


def main() -> int:
    if not SRC.exists():
        print(f"missing source: {SRC}", file=sys.stderr)
        return 1
    text = SRC.read_text()
    code_dims = scan_code_dimensions()
    OUT.mkdir(exist_ok=True)
    (OUT / "oled").mkdir(exist_ok=True)
    (OUT / "paper").mkdir(exist_ok=True)

    items_html: list[str] = []
    count = 0
    for name, w, h, data in parse(text, code_dims):
        if w == 0 or h == 0:
            print(f"skipping image_{name}: unknown dimensions", file=sys.stderr)
            continue
        try:
            pixels = decode_xbm(data, w, h)
        except ValueError as exc:
            print(f"skipping image_{name}: {exc}", file=sys.stderr)
            continue
        oled = render(pixels, lit_color=(255, 255, 255), bg=(0, 0, 0))
        paper = render(pixels, lit_color=(0, 0, 0), bg=(255, 255, 255))
        oled.save(OUT / "oled" / f"{name}.png")
        paper.save(OUT / "paper" / f"{name}.png")
        items_html.append(ITEM_TEMPLATE.format(name=name, w=w, h=h))
        count += 1
        print(f"  {name}: {w}x{h}")

    (OUT / "index.html").write_text(
        HTML_TEMPLATE.format(count=count, items="\n".join(items_html))
    )
    print(f"\nwrote {count} images to {OUT}")
    print(f"open {OUT / 'index.html'} in a browser")
    return 0


if __name__ == "__main__":
    sys.exit(main())
