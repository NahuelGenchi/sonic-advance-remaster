#!/usr/bin/env python3
"""Convert one captured PPM frame to PNG, optionally scaled up.

The capture hook writes binary P6 PPM frames at the port's 426x240 rendering
size. This converter applies no filtering: an upscale repeats whole pixels, so
each committed image keeps exactly the pixels that were captured.

Usage:
    python3 tools/ppm_to_png.py FRAME.ppm docs/images/scene.png --scale 2

Requires Pillow (``python3 -m pip install pillow``).
"""

import argparse

from PIL import Image


def main():
    parser = argparse.ArgumentParser(
        description="Convert a captured PPM frame to PNG without filtering."
    )
    parser.add_argument("source", help="captured .ppm frame")
    parser.add_argument("destination", help="output .png path")
    parser.add_argument(
        "--scale",
        type=int,
        default=2,
        help="nearest-neighbour upscale factor (default: 2)",
    )
    args = parser.parse_args()

    if args.scale < 1:
        parser.error("scale must be at least 1")

    image = Image.open(args.source).convert("RGB")
    if args.scale > 1:
        image = image.resize(
            (image.width * args.scale, image.height * args.scale), Image.NEAREST
        )
    image.save(args.destination)
    print(f"{args.source} -> {args.destination} ({image.width}x{image.height})")


if __name__ == "__main__":
    main()
