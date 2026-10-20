#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Captures the stage 1 reference frames and compares the two renderers.

The helper runs the stage 0 host twice on the same deterministic tape: once
with the upstream software PPU (the reference) and once with the Stage 1 GPU
layer path. It then compares the captured frames pair by pair and prints the
differing pixel count and the maximum channel delta.

    python3 tools/stage1/capture_slice.py --binary /path/to/sa2-stage0 \
        --frames 800,950,1100,1200,1300,1450,1600,1800,2000,2150 \
        --out /tmp/slice

The software run writes the software PPU frame, the GPU run writes the
readback of the presented frame. The frames are verification output and stay
outside the repository.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from compare_ppm import read_ppm  # noqa: E402


def run_capture(binary, frames, out_dir, renderer, save_path, env_extra):
    os.makedirs(out_dir, exist_ok=True)
    env = dict(os.environ)
    env.update({
        "SDL_VIDEODRIVER": "x11",
        "SDL_AUDIODRIVER": "dummy",
        "SAR_VSYNC": "0",
        "SAR_RENDERER": renderer,
        "SAR_CAPTURE_DIR": out_dir,
        "SAR_CAPTURE_FRAME": frames,
        "SAR_EXIT_AFTER_FRAME": frames.split(",")[-1],
        "SAR_SAVE_PATH": save_path,
    })
    env.update(env_extra)

    result = subprocess.run([binary], env=env, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"the {renderer} run failed with status {result.returncode}")

    for line in result.stdout.splitlines():
        if line.startswith("stage1:") or line.startswith("stage0: captured"):
            print(f"  {renderer}: {line}")

    return out_dir


def compare_pair(reference_path, candidate_path):
    ref_w, ref_h, ref = read_ppm(reference_path)
    cand_w, cand_h, cand = read_ppm(candidate_path)

    if (ref_w, ref_h) != (cand_w, cand_h):
        return None, f"size mismatch {ref_w}x{ref_h} vs {cand_w}x{cand_h}"

    differing = 0
    max_delta = 0
    for index in range(ref_w * ref_h):
        base = index * 3
        delta = 0
        for channel in range(3):
            value = abs(ref[base + channel] - cand[base + channel])
            if value > delta:
                delta = value
        if delta:
            differing += 1
            if delta > max_delta:
                max_delta = delta

    return (differing, max_delta), None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="the host binary to run")
    parser.add_argument("--frames", required=True, help="comma separated VBlank numbers")
    parser.add_argument("--out", required=True, help="directory for the captured frames")
    parser.add_argument("--save", default=None, help="save file path (default: <out>/sa2.sav)")
    parser.add_argument("--tolerance", type=int, default=0,
                        help="maximum accepted channel delta (default 0)")
    args = parser.parse_args()

    frames = args.frames
    numbers = [int(value) for value in frames.split(",")]
    save_path = args.save or os.path.join(args.out, "sa2.sav")
    os.makedirs(args.out, exist_ok=True)

    print(f"stage1: capturing frames {numbers} with the software PPU")
    software_dir = run_capture(args.binary, frames, os.path.join(args.out, "software"), "software", save_path, {})

    print(f"stage1: capturing frames {numbers} with the GPU layer path")
    gpu_dir = run_capture(args.binary, frames, os.path.join(args.out, "gl"), "gl", save_path, {})

    failures = 0
    print("stage1: comparison against the software PPU")
    for number in numbers:
        reference = os.path.join(software_dir, f"frame_{number:06d}.ppm")
        candidate = os.path.join(gpu_dir, f"frame_{number:06d}.gl.ppm")
        if not os.path.exists(reference) or not os.path.exists(candidate):
            print(f"  frame {number}: missing capture")
            failures += 1
            continue

        result, error = compare_pair(reference, candidate)
        if error:
            print(f"  frame {number}: {error}")
            failures += 1
            continue

        differing, max_delta = result
        status = "ok" if max_delta <= args.tolerance else "over tolerance"
        print(f"  frame {number}: {differing} differing pixels, max channel delta {max_delta} ({status})")
        if max_delta > args.tolerance:
            failures += 1

    print(f"stage1: {len(numbers) - failures} of {len(numbers)} frames within tolerance {args.tolerance}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
