#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares two binary PPM frames.

Used by the stage 1 slice to compare the GPU layer path with the upstream
software PPU. The tool reports the number of differing pixels, the maximum
channel delta and a histogram of the deltas, and it can write a difference
image for review. It exits non-zero when the frames differ by more than the
given tolerance, so it can be used as a check.

    python3 tools/stage1/compare_ppm.py reference.ppm candidate.ppm
    python3 tools/stage1/compare_ppm.py reference.ppm candidate.ppm --tolerance 2
    python3 tools/stage1/compare_ppm.py reference.ppm candidate.ppm --diff out.ppm
"""

import argparse
import sys


def read_ppm(path):
    with open(path, "rb") as handle:
        if handle.readline().strip() != b"P6":
            raise ValueError(f"{path}: not a binary PPM")
        line = handle.readline()
        while line.startswith(b"#"):
            line = handle.readline()
        width, height = (int(value) for value in line.split())
        maximum = int(handle.readline())
        if maximum != 255:
            raise ValueError(f"{path}: only 8-bit PPM files are supported")
        data = handle.read()
    expected = width * height * 3
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, found {len(data)}")
    return width, height, data


def write_ppm(path, width, height, data):
    with open(path, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (width, height))
        handle.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--tolerance", type=int, default=0,
                        help="maximum accepted channel delta (default 0)")
    parser.add_argument("--diff", help="write a difference image to this path")
    parser.add_argument("--quiet", action="store_true", help="only print the summary line")
    args = parser.parse_args()

    ref_w, ref_h, ref = read_ppm(args.reference)
    cand_w, cand_h, cand = read_ppm(args.candidate)

    if (ref_w, ref_h) != (cand_w, cand_h):
        print(f"size mismatch: reference {ref_w}x{ref_h}, candidate {cand_w}x{cand_h}")
        return 2

    pixels = ref_w * ref_h
    differing = 0
    over_tolerance = 0
    max_delta = 0
    sum_delta = 0
    histogram = {}
    diff = bytearray(pixels * 3)

    for index in range(pixels):
        base = index * 3
        delta = 0
        for channel in range(3):
            value = abs(ref[base + channel] - cand[base + channel])
            if value > delta:
                delta = value
            sum_delta += value
        if delta:
            differing += 1
            histogram[delta] = histogram.get(delta, 0) + 1
            if delta > max_delta:
                max_delta = delta
            if delta > args.tolerance:
                over_tolerance += 1
            diff[base] = min(255, delta * 8)
            diff[base + 1] = 0
            diff[base + 2] = 0
        else:
            diff[base] = ref[base] // 3
            diff[base + 1] = ref[base + 1] // 3
            diff[base + 2] = ref[base + 2] // 3

    if not args.quiet:
        print(f"reference: {args.reference}")
        print(f"candidate: {args.candidate}")
        print(f"size: {ref_w}x{ref_h} ({pixels} pixels)")
        print(f"differing pixels: {differing} ({100.0 * differing / pixels:.4f}%)")
        print(f"pixels over tolerance {args.tolerance}: {over_tolerance}")
        print(f"maximum channel delta: {max_delta}")
        print(f"mean channel delta: {sum_delta / (pixels * 3):.6f}")
        if histogram:
            print("delta histogram:")
            for delta in sorted(histogram):
                print(f"  delta {delta:3d}: {histogram[delta]} pixels")

    if args.diff:
        write_ppm(args.diff, ref_w, ref_h, bytes(diff))
        if not args.quiet:
            print(f"difference image: {args.diff}")

    print(f"compare: {differing} differing pixels, max channel delta {max_delta}, "
          f"{over_tolerance} pixels over tolerance {args.tolerance}")

    return 1 if over_tolerance else 0


if __name__ == "__main__":
    sys.exit(main())
