#!/usr/bin/env python3
"""Analyse the local SDL port performance logs.

The port writes CSV-like lines (meta, present, step, skip, key, event, end).
This tool reports present and game-step cadence, per-frame CPU/GPU cost from a
perf_run.py summary, and input-to-present latency when an injector log is
supplied. All times are CLOCK_MONOTONIC seconds.

Example:
  python3 tools/perf_analyze.py --log /tmp/perf/sa1-60/sa1-60.log \
      --summary /tmp/perf/sa1-60/sa1-60.json --from-step 1200 --to-step 2300
"""

import argparse
import json
import math
import statistics


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


def describe(values):
    if not values:
        return {"n": 0}
    return {
        "n": len(values),
        "mean": statistics.mean(values),
        "stdev": statistics.stdev(values) if len(values) > 1 else 0.0,
        "min": min(values),
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "max": max(values),
    }


def read_log(path):
    data = {
        "meta": {},
        "modes": [],
        "presents": [],
        "steps": [],
        "skips": [],
        "keys": [],
        "events": [],
        "end": None,
    }
    with open(path) as handle:
        for line in handle:
            fields = line.rstrip("\n").split(",")
            kind = fields[0]
            try:
                if kind == "meta" and len(fields) >= 4:
                    if fields[2] == "mode":
                        data["modes"].append(",".join(fields[3:]))
                    else:
                        data["meta"][fields[2]] = ",".join(fields[3:])
                elif kind == "present" and len(fields) >= 10:
                    data["presents"].append(
                        {
                            "n": int(fields[2]),
                            "begin": float(fields[3]),
                            "end": float(fields[4]),
                            "block": float(fields[5]),
                            "step": int(fields[6]),
                            "drawn": int(fields[7]),
                            "available": int(fields[8]),
                            "acc": float(fields[9]),
                            "display": float(fields[10]) if len(fields) >= 11 else float(fields[4]),
                        }
                    )
                elif kind == "step" and len(fields) >= 5:
                    data["steps"].append({"n": int(fields[2]), "t": float(fields[3]), "acc": float(fields[4])})
                elif kind == "skip" and len(fields) >= 5:
                    data["skips"].append({"t": float(fields[2]), "acc": float(fields[3]), "step": int(fields[4])})
                elif kind == "key" and len(fields) >= 7:
                    data["keys"].append(
                        {
                            "n": int(fields[2]),
                            "t": float(fields[3]),
                            "keys": int(fields[4]),
                            "present": int(fields[5]),
                            "step": int(fields[6]),
                        }
                    )
                elif kind == "event" and len(fields) >= 7:
                    data["events"].append(
                        {
                            "type": int(fields[2]),
                            "t": float(fields[3]),
                            "sym": int(fields[4]),
                            "present": int(fields[5]),
                            "step": int(fields[6]),
                        }
                    )
                elif kind == "end":
                    data["end"] = fields
            except (ValueError, IndexError):
                continue
    return data


def in_window(item, args, meta_start):
    if args.from_step is not None or args.to_step is not None:
        step = item.get("step", item.get("n"))
        if args.from_step is not None and step < args.from_step:
            return False
        if args.to_step is not None and step > args.to_step:
            return False
        return True
    if args.from_time is not None or args.to_time is not None:
        when = item.get("begin", item.get("t"))
        relative = when - meta_start
        if args.from_time is not None and relative < args.from_time:
            return False
        if args.to_time is not None and relative > args.to_time:
            return False
    return True


def select(data, args):
    meta_start = float(data["meta"].get("start", 0.0))
    return {
        "presents": [item for item in data["presents"] if in_window(item, args, meta_start)],
        "steps": [item for item in data["steps"] if in_window(item, args, meta_start)],
        "skips": [item for item in data["skips"] if in_window(item, args, meta_start)],
        "keys": [item for item in data["keys"] if in_window(item, args, meta_start)],
        "events": [item for item in data["events"] if in_window(item, args, meta_start)],
    }


def interval_report(items, time_key, nominal_ms):
    values = [item[time_key] for item in items]
    deltas = [(b - a) * 1000.0 for a, b in zip(values, values[1:])]
    report = describe(deltas)
    if deltas:
        report["rate_hz"] = 1000.0 / report["mean"] if report["mean"] else None
        report["late"] = sum(1 for value in deltas if value > 1.5 * nominal_ms)
        report["early"] = sum(1 for value in deltas if value < 0.5 * nominal_ms)
        report["missed_vblanks"] = sum(
            max(0, int(round(value / nominal_ms)) - 1) for value in deltas if value > 1.5 * nominal_ms
        )
    return report


def latency_report(data, inject_log, key_bit, event_sym):
    presses = []
    with open(inject_log) as handle:
        for line in handle:
            fields = line.rstrip("\n").split(",")
            if fields[0] == "press":
                presses.append({"index": int(fields[1]), "t": float(fields[2]), "key": fields[3]})
    events = []
    samples = []
    presents = []
    lost = 0
    for index, press in enumerate(presses):
        # A press is matched to the first KEYDOWN of the injected symbol before
        # the next press. KEYUP, key repeat and unrelated keys are ignored.
        limit = presses[index + 1]["t"] if index + 1 < len(presses) else press["t"] + 1.0
        event = next(
            (
                item
                for item in data["events"]
                if item["type"] == 768
                and item["sym"] == event_sym
                and press["t"] <= item["t"] < limit
            ),
            None,
        )
        if event is None:
            lost += 1
        else:
            events.append((event["t"] - press["t"]) * 1000.0)
        sample = next(
            (item for item in data["keys"] if item["t"] >= press["t"] and (item["keys"] & key_bit)),
            None,
        )
        if sample is None:
            continue
        samples.append((sample["t"] - press["t"]) * 1000.0)
        present = next(
            (
                item
                for item in data["presents"]
                if item["step"] >= sample["step"] + 1 and item["display"] >= sample["t"]
            ),
            None,
        )
        if present is not None:
            presents.append((present["display"] - press["t"]) * 1000.0)
            presents.append((present["begin"] - press["t"]) * 1000.0)
    present_values = presents[0::2]
    present_begin_values = presents[1::2]
    return {
        "presses": len(presses),
        "lost_presses": lost,
        "event_sym": event_sym,
        "event_to_poll_ms": describe(events),
        "press_to_sample_ms": describe(samples),
        "press_to_present_begin_ms": describe(present_begin_values),
        "press_to_present_end_ms": describe(present_values),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True)
    parser.add_argument("--summary", default="", help="perf_run.py JSON summary")
    parser.add_argument("--inject-log", default="")
    parser.add_argument("--key-bit", type=lambda value: int(value, 0), default=1)
    parser.add_argument("--event-sym", type=int, default=99, help="SDL keysym expected from the injector")
    parser.add_argument("--from-step", type=int)
    parser.add_argument("--to-step", type=int)
    parser.add_argument("--from-time", type=float)
    parser.add_argument("--to-time", type=float)
    parser.add_argument("--nominal-ms", type=float, default=1000.0 / 60.0)
    parser.add_argument("--json-out", default="")
    args = parser.parse_args()

    data = read_log(args.log)
    window = select(data, args)
    nominal = args.nominal_ms
    pace = int(data["meta"].get("pace_hz", "0") or 0)
    if pace > 0:
        nominal = 1000.0 / pace

    steps = interval_report(window["steps"], "t", nominal)
    presents = interval_report(window["presents"], "display", nominal)
    blocks = [item["block"] * 1000.0 for item in window["presents"]]
    step_times = [item["t"] for item in window["steps"]]
    step_span = step_times[-1] - step_times[0] if len(step_times) > 1 else 0.0
    present_times = [item["display"] for item in window["presents"]]
    present_span = present_times[-1] - present_times[0] if len(present_times) > 1 else 0.0

    result = {
        "log": args.log,
        "meta": data["meta"],
        "modes": data["modes"],
        "window": {
            "from_step": args.from_step,
            "to_step": args.to_step,
            "from_time": args.from_time,
            "to_time": args.to_time,
        },
        "steps": steps,
        "presents": presents,
        "present_block_ms": describe(blocks),
        "game_steps": len(window["steps"]),
        "game_step_rate_hz": (len(window["steps"]) - 1) / step_span if step_span else None,
        "presents_per_step": len(window["presents"]) / len(window["steps"]) if window["steps"] else None,
        "drawn_presents": sum(1 for item in window["presents"] if item["drawn"]),
        "skips": len(window["skips"]),
        "keys": len(window["keys"]),
        "events": len(window["events"]),
    }

    if args.summary:
        with open(args.summary) as handle:
            summary = json.load(handle)
        result["summary_tag"] = summary.get("tag")
        t0 = summary.get("sample_t0")
        t1 = summary.get("sample_t1")
        if t0 is not None and t1 is not None:
            sample_presents = [item for item in data["presents"] if t0 <= item["end"] <= t1]
            sample_steps = [item for item in data["steps"] if t0 <= item["t"] <= t1]
        else:
            sample_presents = data["presents"]
            sample_steps = data["steps"]
        wall = summary.get("sample_window_s") or (t1 - t0 if t0 is not None else 0.0)
        cpu = summary.get("cpu", {}).get("seconds")
        gpu = summary.get("gpu", {}).get("gfx_seconds")
        result["cost"] = {
            "wall_s": wall,
            "cpu_s": cpu,
            "cpu_fraction_of_one_core": summary.get("cpu", {}).get("fraction_of_one_core"),
            "gpu_gfx_s": gpu,
            "gpu_fraction_of_wall": summary.get("gpu", {}).get("fraction_of_wall"),
            "sample_presents": len(sample_presents),
            "sample_steps": len(sample_steps),
            "cpu_ms_per_present": (cpu * 1000.0 / len(sample_presents)) if cpu and sample_presents else None,
            "cpu_ms_per_step": (cpu * 1000.0 / len(sample_steps)) if cpu and sample_steps else None,
            "gpu_ms_per_present": (gpu * 1000.0 / len(sample_presents)) if gpu and sample_presents else None,
            "gpu_ms_per_step": (gpu * 1000.0 / len(sample_steps)) if gpu and sample_steps else None,
            "vram_kib_end": summary.get("gpu", {}).get("vram_kib_end"),
            "global_gpu_busy_percent": summary.get("global_gpu_busy_percent"),
        }

    if args.inject_log:
        result["latency"] = latency_report(data, args.inject_log, args.key_bit, args.event_sym)

    if args.json_out:
        with open(args.json_out, "w") as handle:
            json.dump(result, handle, indent=2)
            handle.write("\n")

    meta = data["meta"]
    tag = result.get("summary_tag") or meta.get("tag", "?")
    print(f"tag: {tag}  renderer: {meta.get('renderer', '?')}  driver: {meta.get('video_driver', '?')}")
    print(f"display: {meta.get('display', '?')}  vsync: {meta.get('vsync_request', 'default')}  pace: {meta.get('pace_hz', '0')} Hz")
    print(f"steps in window: {result['game_steps']} at {result['game_step_rate_hz']:.3f} Hz")
    print(
        f"presents in window: {presents.get('n', 0)} at {presents.get('rate_hz') or 0:.3f} Hz"
        f"  ({result['presents_per_step']:.3f} per step, {result['drawn_presents']} drawn)"
    )
    print(
        "present interval ms: "
        f"mean {presents.get('mean', 0):.3f}  stdev {presents.get('stdev', 0):.3f}  "
        f"p50 {presents.get('p50', 0):.3f}  p95 {presents.get('p95', 0):.3f}  "
        f"p99 {presents.get('p99', 0):.3f}  max {presents.get('max', 0):.3f}  "
        f"late {presents.get('late', 0)}  missed {presents.get('missed_vblanks', 0)}"
    )
    print(
        "step interval ms:    "
        f"mean {steps.get('mean', 0):.3f}  stdev {steps.get('stdev', 0):.3f}  "
        f"p95 {steps.get('p95', 0):.3f}  p99 {steps.get('p99', 0):.3f}  max {steps.get('max', 0):.3f}"
    )
    print(f"SDL_RenderPresent block ms: mean {describe(blocks).get('mean', 0):.3f}  p95 {describe(blocks).get('p95', 0):.3f}  max {describe(blocks).get('max', 0):.3f}")
    if "cost" in result:
        cost = result["cost"]
        print(
            f"CPU: {cost['cpu_s']:.3f} s over {cost['wall_s']:.3f} s "
            f"({cost['cpu_fraction_of_one_core'] * 100:.1f}% of one core), "
            f"{cost['cpu_ms_per_present']:.3f} ms/present, {cost['cpu_ms_per_step']:.3f} ms/step"
        )
        print(
            f"GPU: {cost['gpu_gfx_s']:.3f} s gfx engine "
            f"({cost['gpu_fraction_of_wall'] * 100:.1f}% of wall), "
            f"{cost['gpu_ms_per_present']:.3f} ms/present, {cost['gpu_ms_per_step']:.3f} ms/step, "
            f"VRAM {cost['vram_kib_end']} KiB"
        )
        busy = cost["global_gpu_busy_percent"]
        print(f"global GPU busy: mean {busy['mean']}%  max {busy['max']}%  ({busy['samples']} samples)")
    if "latency" in result:
        latency = result["latency"]
        print(f"input presses: {latency['presses']}  lost: {latency['lost_presses']}  sym: {latency['event_sym']}")
        for key in ("event_to_poll_ms", "press_to_sample_ms", "press_to_present_begin_ms", "press_to_present_end_ms"):
            stats = latency[key]
            if stats.get("n"):
                print(
                    f"  {key}: n {stats['n']}  mean {stats['mean']:.3f}  p50 {stats['p50']:.3f}  "
                    f"p95 {stats['p95']:.3f}  max {stats['max']:.3f}"
                )
            else:
                print(f"  {key}: no matches")


if __name__ == "__main__":
    main()
