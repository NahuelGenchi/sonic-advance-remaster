#!/usr/bin/env python3
"""Run one SDL port under the local performance instrumentation.

The runner samples process CPU time, per-process amdgpu engine time from
/proc/<pid>/fdinfo, and global GPU busy percent from sysfs. It does not record
the game window and does not copy ROMs, saves or builds into the repository.

Example:
  python3 tools/perf_run.py --binary /tmp/sa1-perf.sdl --tag sa1-60-vsync \
      --outdir /tmp/perf/sa1-60-vsync --seconds 40 --vsync 1 --script sa1
"""

import argparse
import glob
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import threading
import time

CLOCK_TICKS = os.sysconf("SC_CLK_TCK")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_cpu_seconds(pid):
    total = 0
    for path in glob.glob(f"/proc/{pid}/task/*/stat"):
        try:
            text = open(path).read()
        except OSError:
            continue
        close = text.rfind(")")
        fields = text[close + 2 :].split()
        if len(fields) < 13:
            continue
        total += int(fields[11]) + int(fields[12])
    return total / CLOCK_TICKS


def read_gpu(pid):
    """Return (gfx_ns, vram_kib) for the process's unique DRM clients."""
    clients = {}
    for path in glob.glob(f"/proc/{pid}/fdinfo/*"):
        try:
            text = open(path).read()
        except OSError:
            continue
        if "drm-client-id" not in text:
            continue
        values = {}
        for line in text.splitlines():
            key, _, value = line.partition(":")
            values[key.strip()] = value.strip()
        client = values.get("drm-client-id")
        if client is None:
            continue
        gfx = values.get("drm-engine-gfx", "0 ns").split()[0]
        vram = values.get("drm-memory-vram", "0 KiB").split()[0]
        try:
            gfx = int(gfx)
            vram = int(vram)
        except ValueError:
            continue
        previous = clients.get(client, (0, 0))
        clients[client] = (max(previous[0], gfx), max(previous[1], vram))
    gfx_ns = sum(value[0] for value in clients.values())
    vram_kib = sum(value[1] for value in clients.values())
    return gfx_ns, vram_kib, len(clients)


def gpu_busy_path():
    for path in glob.glob("/sys/class/drm/card*/device/gpu_busy_percent"):
        return path
    return None


def read_int(path, default=None):
    try:
        return int(open(path).read().strip())
    except (OSError, ValueError):
        return default


def sample_global_gpu(stop_event, samples):
    path = gpu_busy_path()
    if path is None:
        return
    while not stop_event.is_set():
        value = read_int(path)
        if value is not None:
            samples.append((time.monotonic(), value))
        stop_event.wait(0.05)


def parse_game_log(path):
    """Summarise the local instrumentation log without loading every line."""
    summary = {
        "meta": {},
        "modes": [],
        "presents": 0,
        "steps": 0,
        "skips": 0,
        "keys": 0,
        "events": 0,
        "first_present": None,
        "last_present": None,
        "first_step": None,
        "last_step": None,
    }
    if not os.path.exists(path):
        return summary
    with open(path) as handle:
        for line in handle:
            fields = line.rstrip("\n").split(",")
            kind = fields[0]
            if kind == "meta" and len(fields) >= 4:
                key = fields[2]
                value = ",".join(fields[3:])
                if key == "mode":
                    summary["modes"].append(value)
                else:
                    summary["meta"][key] = value
            elif kind == "present" and len(fields) >= 5:
                summary["presents"] += 1
                when = float(fields[10]) if len(fields) >= 11 else float(fields[4])
                if summary["first_present"] is None:
                    summary["first_present"] = when
                summary["last_present"] = when
            elif kind == "step" and len(fields) >= 4:
                summary["steps"] += 1
                when = float(fields[3])
                if summary["first_step"] is None:
                    summary["first_step"] = when
                summary["last_step"] = when
            elif kind == "skip":
                summary["skips"] += 1
            elif kind == "key":
                summary["keys"] += 1
            elif kind == "event":
                summary["events"] += 1
    for key in ("presents", "steps", "skips", "keys", "events"):
        if key in summary and summary[key] is None:
            summary[key] = 0
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--outdir", required=True)
    parser.add_argument("--seconds", type=float, default=40.0)
    parser.add_argument("--warmup", type=float, default=2.0)
    parser.add_argument("--cooldown", type=float, default=1.0)
    parser.add_argument("--vsync", type=int, choices=(0, 1), default=1)
    parser.add_argument("--pace", type=int, default=0, help="synthetic present cadence in Hz")
    parser.add_argument("--script", default="", help="sa1 for the documented scripted run")
    parser.add_argument("--no-log", action="store_true", help="run without PERF_LOG")
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    if args.warmup + args.cooldown >= args.seconds:
        parser.error("warmup + cooldown must be shorter than seconds")

    os.makedirs(args.outdir, exist_ok=True)
    binary = os.path.abspath(args.binary)
    log_path = os.path.join(args.outdir, f"{args.tag}.log")
    samples_path = os.path.join(args.outdir, f"{args.tag}.gpu-samples.csv")
    summary_path = os.path.join(args.outdir, f"{args.tag}.json")

    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "x11"
    env["SDL_RENDER_DRIVER"] = "opengl"
    env["SDL_AUDIODRIVER"] = "dummy"
    env["vblank_mode"] = "1" if args.vsync else "0"
    env["PERF_TAG"] = args.tag
    env["PERF_VSYNC"] = str(args.vsync)
    env["PERF_PACE_HZ"] = str(args.pace)
    if args.script:
        env["PERF_SCRIPT"] = args.script
    if args.no_log:
        env.pop("PERF_LOG", None)
    else:
        env["PERF_LOG"] = log_path
        env["PERF_SECONDS"] = str(args.seconds)

    command = [binary]
    started = time.monotonic()
    process = subprocess.Popen(command, cwd=args.outdir, env=env)
    time.sleep(args.warmup)
    pid = process.pid

    gpu_samples = []
    stop_event = threading.Event()
    sampler = threading.Thread(target=sample_global_gpu, args=(stop_event, gpu_samples), daemon=True)
    sampler.start()

    cpu0 = read_cpu_seconds(pid)
    gpu0, vram0, clients0 = read_gpu(pid)
    t0 = time.monotonic()
    sample_end = started + args.seconds - args.cooldown
    time.sleep(max(0.0, sample_end - time.monotonic()))
    t1 = time.monotonic()
    cpu1 = read_cpu_seconds(pid)
    gpu1, vram1, clients1 = read_gpu(pid)
    stop_event.set()
    sampler.join(timeout=1.0)

    with open(samples_path, "w") as handle:
        handle.write("monotonic_s,busy_percent\n")
        for when, value in gpu_samples:
            handle.write(f"{when:.6f},{value}\n")

    grace = 0.5 if args.no_log else 5.0
    remaining = started + args.seconds + grace - time.monotonic()
    try:
        returncode = process.wait(timeout=max(1.0, remaining))
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            returncode = process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            returncode = process.wait()

    window_busy = [value for when, value in gpu_samples if t0 <= when <= t1]
    gfx_ns = gpu1 - gpu0
    gpu_seconds = gfx_ns / 1e9
    cpu_seconds = cpu1 - cpu0
    wall = t1 - t0
    log = parse_game_log(log_path) if not args.no_log else None
    summary = {
        "tag": args.tag,
        "notes": args.notes,
        "binary": binary,
        "binary_sha256": sha256(binary),
        "command": command,
        "seconds": args.seconds,
        "warmup_s": args.warmup,
        "cooldown_s": args.cooldown,
        "sample_window_s": wall,
        "sample_t0": t0,
        "sample_t1": t1,
        "returncode": returncode,
        "env": {
            "SDL_VIDEODRIVER": env["SDL_VIDEODRIVER"],
            "SDL_RENDER_DRIVER": env["SDL_RENDER_DRIVER"],
            "SDL_AUDIODRIVER": env["SDL_AUDIODRIVER"],
            "vblank_mode": env["vblank_mode"],
            "PERF_VSYNC": env["PERF_VSYNC"],
            "PERF_PACE_HZ": env["PERF_PACE_HZ"],
            "PERF_SCRIPT": env.get("PERF_SCRIPT", ""),
        },
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "cpu_count": os.cpu_count(),
            "cpu_model": next(
                (line.split(":", 1)[1].strip() for line in open("/proc/cpuinfo") if line.startswith("model name")),
                "",
            ),
            "python": platform.python_version(),
        },
        "cpu": {
            "seconds": cpu_seconds,
            "fraction_of_one_core": cpu_seconds / wall if wall else None,
        },
        "gpu": {
            "gfx_seconds": gpu_seconds,
            "fraction_of_wall": gpu_seconds / wall if wall else None,
            "vram_kib_start": vram0,
            "vram_kib_end": vram1,
            "clients_start": clients0,
            "clients_end": clients1,
        },
        "global_gpu_busy_percent": {
            "mean": statistics.mean(window_busy) if window_busy else None,
            "max": max(window_busy) if window_busy else None,
            "samples": len(window_busy),
        },
        "log": log,
    }
    with open(summary_path, "w") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
