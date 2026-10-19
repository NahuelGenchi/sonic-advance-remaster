#!/usr/bin/env python3
"""Tile captured PPM frames into labeled PNG contact sheets for review.

The capture directory holds the PPM files written by the headless capture hook
described in ``docs/CAPTURE.md``. Every sheet shows one tile per frame with the
frame number printed below it, so a long run can be reviewed at a glance.

Usage:
    python3 tools/make_contact_sheet.py CAPTURE_DIR OUTPUT_DIR
        [--columns 10] [--rows 8] [--scale 0.3] [--pattern frame_*.ppm]

Requires Pillow (``python3 -m pip install pillow``). The tool reads frames and
writes only the sheets; the PPM files are left untouched.
"""

import argparse
import glob
import os

from PIL import Image, ImageDraw

LABEL_HEIGHT = 14
BACKGROUND = (18, 18, 18)
LABEL_COLOR = (210, 210, 210)


def build_sheet(files, columns, rows, scale, output_path):
    probe = Image.open(files[0])
    width, height = probe.size
    tile_width = max(1, int(width * scale))
    tile_height = max(1, int(height * scale))

    sheet = Image.new(
        "RGB",
        (tile_width * columns, (tile_height + LABEL_HEIGHT) * rows),
        BACKGROUND,
    )
    draw = ImageDraw.Draw(sheet)

    for index, path in enumerate(files):
        frame = Image.open(path).convert("RGB")
        frame = frame.resize((tile_width, tile_height), Image.NEAREST)
        x = (index % columns) * tile_width
        y = (index // columns) * (tile_height + LABEL_HEIGHT)
        sheet.paste(frame, (x, y))
        label = os.path.splitext(os.path.basename(path))[0]
        draw.text((x + 3, y + tile_height + 1), label, fill=LABEL_COLOR)

    sheet.save(output_path)


def main():
    parser = argparse.ArgumentParser(
        description="Tile captured PPM frames into labeled PNG contact sheets."
    )
    parser.add_argument("capture_dir", help="directory holding the captured frames")
    parser.add_argument("output_dir", help="directory for the generated sheets")
    parser.add_argument("--columns", type=int, default=10)
    parser.add_argument("--rows", type=int, default=8)
    parser.add_argument("--scale", type=float, default=0.3)
    parser.add_argument("--pattern", default="frame_*.ppm")
    args = parser.parse_args()

    if args.columns < 1 or args.rows < 1 or args.scale <= 0:
        parser.error("columns, rows and scale must be positive")

    files = sorted(glob.glob(os.path.join(args.capture_dir, args.pattern)))
    if not files:
        parser.error(f"no files match {args.pattern!r} in {args.capture_dir}")

    os.makedirs(args.output_dir, exist_ok=True)
    per_sheet = args.columns * args.rows
    sheets = 0

    for start in range(0, len(files), per_sheet):
        batch = files[start:start + per_sheet]
        output = os.path.join(args.output_dir, f"sheet_{sheets:03d}.png")
        build_sheet(batch, args.columns, args.rows, args.scale, output)
        sheets += 1

    print(f"{len(files)} frame(s) -> {sheets} sheet(s) in {args.output_dir}")


if __name__ == "__main__":
    main()
