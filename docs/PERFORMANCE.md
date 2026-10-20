# Performance baseline

[Back to README](../README.md) · [Capture](CAPTURE.md) · [Tracking](TRACKING.md)

This is the measured frame-pacing, cost and input-latency baseline for the two
upstream SDL ports, recorded on 2026-10-19 before the M2 renderer work starts.
Every number comes from a run listed below. The summarised rows are committed in
`docs/perf/`; raw logs, binaries, ROMs and saves stayed outside this repository.

Nothing here measures the M2 renderer, which does not exist yet. The numbers
describe the unmodified upstream ports at their current 426x240 client area.

## At a glance

| Question | Result |
| :--- | :--- |
| Do both ports hold 60 Hz with vsync? | Yes. SA1 59.997 presents/s with 1 late present in 17.5 s; SA2 TAS 59.795 presents/s with 4 late presents in 20 s. |
| What does a 60 Hz step cost? | 0.651 ms CPU and 0.125 ms GPU on SA1; 0.698 ms CPU and 0.139 ms GPU on SA2 TAS, per game step. |
| What is the present ceiling? | About 14.5k presents/s with vsync off, with the logic still stepping at 60 Hz. |
| What does a 120 Hz cadence cost? | 1.52-1.54 ms CPU and 0.22-0.24 ms GPU per step, at a clean 2 presents per step. |
| What does a 144 Hz cadence cost? | 1.71-1.79 ms CPU and 0.26 ms GPU per step, at 2.4 presents per step. |
| Input-to-present at 60 Hz | About 25.4-25.5 ms from the synthetic press to the post-swap timestamp, mean over 20 presses. |
| Is 120/144 Hz display latency measured? | No. The host has no such mode; those rows are a labelled paced-present model. |

## Host and stack

| Item | Value used here |
| :--- | :--- |
| CPU | Intel Core i7-14700KF, 20 cores / 28 threads |
| GPU | AMD Radeon R9 380 Series (Tonga), `amdgpu`/`radeonsi` |
| GPU user space | Mesa 26.0.8, OpenGL 4.6 core and compatibility |
| Kernel | Linux 7.0.0-38-generic |
| Session | GNOME Shell 50.1 on Wayland; the game runs through XWayland |
| Display | 1920x1080 at 59.96 Hz; all exposed modes are ~60 Hz, with no 120 or 144 Hz mode |
| SDL | 2.32.10, video driver `x11`, renderer `opengl`, renderer flags `0xe` |
| Game window | 426x240 client area (the port's widescreen size), inside a 482x328 mutter frame |
| Compiler | gcc 15.2.0 (Ubuntu 15.2.0-16ubuntu1) |
| Make | GNU Make 4.4.1 |
| Upstream | `SAT-R/sa2` at `393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d`, cloned outside the repository |
| ROMs | the user-local dumps documented in [CAPTURE.md](CAPTURE.md); never copied into the repository |

The host is one machine and one driver stack. The per-process GPU time is the
`gfx` engine time exposed by `amdgpu` through `/proc/<pid>/fdinfo`; the global
`gpu_busy_percent` is reported only as context because the desktop, compositor
and other applications also use the GPU. `perf_event_paranoid` is 4, so no
hardware performance counters were available.

## Builds

Both ports were built from the pinned upstream checkout with the user's own ROMs
copied in as `baserom_sa1.gba` and `baserom_sa2.gba`:

```sh
make -j"$(nproc)" sdl GAME_NAME=sa1
make -j"$(nproc)" sdl GAME_NAME=sa2
make -j"$(nproc)" sdl GAME_NAME=sa2 TAS_TESTING=1   # same as: make tas_sdl
```

The instrumented binaries used for the measurements are the same builds after a
local, uncommitted patch to `src/platform/pret_sdl/sdl2.c`. The patch adds
timestamped logging around the existing event, step and present code, a
synthetic present pacer (`PERF_PACE_HZ`) and a deterministic SA1 input script
(`PERF_SCRIPT`); it does not change the game logic or the renderer. It is not in
this repository and is not proposed upstream. The tools that drive and analyse
the runs are committed here: `tools/perf_run.py`, `tools/perf_inject.py` and
`tools/perf_analyze.py`.

Switching between the TAS and non-TAS SA2 builds needs a clean rebuild:
`TAS_TESTING` is not part of the object paths, so a stale object tree can link
the wrong input path.

## What was measured

`tools/perf_run.py` launches one binary, sets the SDL environment
(`SDL_VIDEODRIVER=x11`, `SDL_RENDER_DRIVER=opengl`, dummy audio), and samples:

- CPU: user plus system time of every thread in `/proc/<pid>/task/*/stat`.
- GPU: the `drm-engine-gfx` time of the process's DRM clients in
  `/proc/<pid>/fdinfo/*`, summed per client and reported as engine seconds.
- Global GPU: `gpu_busy_percent` from sysfs, sampled every 50 ms.

Each baseline run lasts 42 s: 20 s warm-up, a 21 s sampling window and a 1 s
cooldown. Pacing distributions use the analysis window from game step 1200 to
2400 or the end of the log: 20 s for SA2 TAS and 17.5 s for SA1, whose log ends
at step 2249. Cost figures use the full 21 s sampling window. One baseline run
was recorded per configuration; each latency case is 20 presses.

The local instrumentation writes CSV-like lines with `CLOCK_MONOTONIC` times:

| Line | Fields |
| :--- | :--- |
| `meta` | tag, key, value: driver, renderer, display mode, vsync request/result, swap interval, paced rate |
| `present` | frame number, begin, end, block time, step, drawn flag, available flag, accumulator, display time |
| `step` | game-step number, time, accumulator |
| `skip` | time, accumulator, step: the loop asked for a new game frame |
| `key` | key-change number, time, pressed button mask, present and step counters |
| `event` | SDL event type, time, keysym, present and step counters |
| `end` | final time and counters |

`display` is the post-swap timestamp normally, and the post-pacing deadline when
`PERF_PACE_HZ` is set. A `skip` is not a dropped frame: it records the loop
returning to the game to produce the next frame. Because the logic steps at
60 Hz, a presents/step value above 1 means the same game frame is presented more
than once; the 120/144 rows present each 60 Hz frame two or 2.4 times by
construction.

### Vsync and pacing

The 60 Hz runs request vsync with `SDL_RenderSetVSync(1)` and `vblank_mode=1`.
The meta line reports `vsync_request,1,result,0` and `swap_interval,1`; the
present block takes about 16.1 ms, which is the swap waiting for the panel.

The host exposes no 120 or 144 Hz mode. The 120 and 144 Hz rows therefore run
with vsync off and use `PERF_PACE_HZ` to sleep after each present until the next
synthetic deadline. They are a **paced-present model**, not a measurement of a
120 or 144 Hz display: the panel is still 60 Hz and the post-pacing timestamp is
the model's display point, not a scan-out. The 144 Hz model is additionally not
an integer multiple of the 60 Hz logic step; that is discussed below.

### Scenes

- SA1 baseline: the documented scripted run (`PERF_SCRIPT=sa1`), which starts
  the game and holds right plus periodic A after step 1100. The analysis window
  is in that scripted gameplay.
- SA2 baseline: the SA2 TAS build with its recorded input tape; the game plays
  itself.
- SA1 and SA2 latency: no script. The latency runs use the non-TAS SA2 build and
  the title/attract state.

The two ports were not run through the same scene, so the port-to-port numbers
are not a controlled A/B comparison. Each row is the documented scene for that
port.

## Frame pacing and cost at 60 Hz with vsync

| Run | Presents/s | Steps/s | Presents/step | Present mean (ms) | p95 | p99 | Max | Late / missed | Step mean (ms) | Step sd | Step p99 | Present block (ms) | CPU s | CPU % one core | CPU ms/step | GPU s | GPU % wall | GPU ms/step | VRAM |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SA1 60 vsync | 59.997 | 59.998 | 1.000 | 16.668 | 16.903 | 17.726 | 27.085 | 1 / 1 | 16.667 | 0.705 | 18.117 | 16.102 | 0.820 | 3.9% | 0.651 | 0.157 | 0.8% | 0.125 | 12.7 MiB |
| SA2 TAS 60 vsync | 59.795 | 59.996 | 0.997 | 16.724 | 16.955 | 18.466 | 33.303 | 4 / 4 | 16.668 | 1.253 | 18.458 | 16.115 | 0.880 | 4.2% | 0.698 | 0.176 | 0.8% | 0.139 | 12.7 MiB |

Late means a present interval above 1.5x the 16.667 ms nominal; missed is the
estimated number of skipped vblanks in those intervals. The analysis window has
1049 presents for SA1 and 1196 for SA2 TAS. CPU and GPU seconds are over the
21 s sampling window, so the per-step columns divide by the steps in that window
(about 1260), not by the 1049/1196 presents in the shorter pacing window.

Both ports hold the 60 Hz cadence. SA1 has one late present interval, estimated
as one missed vblank, and SA2 has four; the longest SA2 interval is 33.3 ms,
about two panel periods. The present block is about 16.1 ms in both, so the swap
is blocking on vsync rather than the renderer taking that long. The CPU cost is
0.65-0.70 ms per step (3.9-4.2% of one core) and the GPU cost is
0.125-0.139 ms per step (0.8% of wall). At a 16.667 ms step budget, that is
roughly 4% of one core and under 1% of the GPU before any M2 work.

The global GPU busy mean was 4.6% for SA1 and 3.3% for SA2 TAS in these windows,
but it includes the compositor and other desktop clients and is not used as the
port's cost.

## Unlocked present ceiling

With vsync off (`vblank_mode=0`, `SDL_RenderSetVSync(0)`) and no pacing:

| Run | Presents/s | Presents/step | Present mean (ms) | Present p95 (ms) | Present block (ms) | CPU % one core | CPU ms/step | GPU % wall | GPU ms/step | Global GPU mean |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SA1 60 unlocked | 14,669 | 244.48 | 0.068 | 0.070 | 0.063 | 40.7% | 6.783 | 40.4% | 6.743 | 97.1% |
| SA2 TAS 60 unlocked | 14,477 | 241.28 | 0.069 | 0.074 | 0.063 | 48.2% | 8.032 | 40.0% | 6.671 | 95.2% |

This is a present-throughput ceiling, not a playable mode. The logic still steps
at 60 Hz and the same game frame is presented about 241-244 times per step. The
renderer sustains about 14.5k presents/s at 426x240, while the global GPU is
near 95-97%; the per-process engine time is 40% of wall, so the rest of the
global figure is desktop and compositor work. The useful conclusion is that the
present path is not the limiter at 120 or 144 presents/s; the 60 Hz logic loop
is.

## 120 Hz and 144 Hz paced models

| Run | Presents/s | Presents/step | Present mean (ms) | Present sd | Present late / missed | Step mean (ms) | Step sd | Step p95 (ms) | CPU % one core | CPU ms/step | GPU % wall | GPU ms/step |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SA1 120 paced | 120.000 | 2.000 | 8.333 | 0.001 | 0 / 0 | 16.667 | 0.090 | 16.723 | 9.2% | 1.540 | 1.3% | 0.222 |
| SA1 144 paced | 144.000 | 2.400 | 6.944 | 0.002 | 0 / 0 | 16.667 | 3.407 | 20.855 | 10.2% | 1.706 | 1.6% | 0.262 |
| SA2 TAS 120 paced | 120.000 | 2.000 | 8.333 | 0.000 | 0 / 0 | 16.667 | 0.028 | 16.692 | 9.1% | 1.524 | 1.4% | 0.242 |
| SA2 TAS 144 paced | 144.000 | 2.400 | 6.944 | 0.001 | 0 / 0 | 16.667 | 3.415 | 20.869 | 10.8% | 1.794 | 1.5% | 0.256 |

The 120 Hz model divides evenly into the 60 Hz logic step: two presents per
step. The present cadence is flat to a few microseconds and the step interval
standard deviation is 0.03-0.09 ms. This is the clean high-refresh datapoint.

The 144 Hz model does not divide evenly: 144/60 = 2.4, so the pacer alternates
two and three presents per step. The present cadence itself stays flat at
6.944 ms, but the 60 Hz step spacing beats in a 3:2 pattern. In the 1200-interval
window, both ports have 720 step intervals below 15 ms and 480 at or above
15 ms, with a step standard deviation of 3.4 ms and a p95 near 20.9 ms. This is
a property of pacing a 60 Hz loop at 144 Hz, not evidence that a real 144 Hz
display would show the same timing; the real-display case is a gap.

Cost rises with the number of presents because every present clears and copies
the frame again: 1.52-1.54 ms CPU per step at 120 Hz and 1.71-1.79 ms at 144 Hz,
against 0.65-0.70 ms at 60 Hz. The GPU stays under 1.6% of wall in both models.
No present interval was late or missed in either model.

## Input latency

`tools/perf_inject.py` creates a temporary `uinput` keyboard, records the press
time with `CLOCK_MONOTONIC` immediately before writing the key-down event, and
presses `c` 20 times per run at 213 ms intervals with a 40 ms hold. The game logs
each `SDL_KEYDOWN`/`SDL_KEYUP` when it polls it, and logs the sampled button mask
when the game step reads it. A press is matched to the first KEYDOWN of that
symbol before the next press; key-up and unrelated keys are ignored.

| Run | Mode | Presses | Lost | Event to poll mean / p95 (ms) | Press to sample mean / p95 (ms) | Press to display mean / p50 / p95 / max (ms) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: |
| SA1 | 60 Hz vsync | 20 | 0 | 10.0 / 17.3 | 10.0 / 17.3 | 25.5 / 26.3 / 33.1 / 33.1 |
| SA1 | 120 Hz paced model | 20 | 0 | 5.0 / 8.0 | 9.1 / 15.5 | 17.1 / 17.3 / 23.8 / 23.9 |
| SA1 | 144 Hz paced model | 20 | 0 | 4.8 / 7.7 | 9.4 / 17.7 | 15.8 / 14.7 / 24.7 / 27.0 |
| SA2 | 60 Hz vsync | 20 | 0 | 9.8 / 16.5 | 9.8 / 16.5 | 25.4 / 25.7 / 32.0 / 32.6 |
| SA2 | 120 Hz paced model | 20 | 0 | 5.9 / 9.0 | 9.5 / 15.6 | 17.3 / 17.9 / 24.0 / 25.1 |
| SA2 | 144 Hz paced model | 20 | 0 | 4.8 / 7.8 | 8.9 / 16.8 | 15.4 / 14.7 / 23.7 / 25.7 |

The three stages are:

1. **Event to poll**: from the synthetic press to the game's `SDL_KEYDOWN`
   timestamp. This includes the uinput/libinput/compositor/XWayland path.
2. **Press to sample**: from the press to the game step that reads the button
   mask. At 60 Hz this is the same as the event time; at 120/144 Hz it is one to
   a few milliseconds later because the game samples input on its 60 Hz step,
   not on every present.
3. **Press to display**: from the press to the post-swap timestamp at 60 Hz, or
   to the paced deadline at 120/144 Hz.

At 60 Hz the mean is about 25.4-25.5 ms from press to the post-swap timestamp,
with a p95 of 32-33 ms. The event-to-poll stage is about 10 ms of that, and the
rest is the 16.7 ms present interval. This is a real vsync path on a real 60 Hz
panel, though it is the swap return, not a photodiode measurement of scan-out.

At the paced 120/144 Hz rates the event-to-poll stage halves to about 5 ms, but
the press-to-sample stage stays near 9 ms. The game still runs its logic at
60 Hz, so a faster present cadence cannot remove the 60 Hz input-sampling
quantization. The press-to-display numbers fall to 15.4-17.3 ms, but those are
the pacing deadlines, not measured scan-out on a 120/144 Hz panel.

The first latency matrix produced no events because the game window was
occluded and did not have the compositor's keyboard focus; those runs are not
used. A later SA2 60 Hz run started while the game was still in its boot/title
transition and hit a 0.44 s input stall: two presses were delivered late while
presents continued. That run is preserved in `docs/perf/latency-stall.csv` and
excluded from the table. Re-running with injection 12 s after launch produced
the clean SA2 60 Hz row above. The failure mode is a reminder that a latency run
is only valid when the window is focused and the scene is past loading.

## Instrumentation overhead and clean controls

Clean binaries without the instrumentation were also run at 60 Hz:

| Run | CPU s | CPU % one core | GPU s | GPU % wall | VRAM |
| :--- | ---: | ---: | ---: | ---: | ---: |
| SA1 clean, 60 vsync | 1.10 | 5.2% | 0.169 | 0.8% | 13.2 MiB |
| SA2 TAS clean, 60 vsync | 1.16 | 5.5% | 0.187 | 0.9% | 13.2 MiB |

These are not an overhead correction. The SA1 clean run stays at the title
screen while the instrumented SA1 run uses the scripted gameplay scene; the SA2
clean and instrumented runs both play the TAS tape, but the clean run used more
CPU. Run-to-run and scene/timing differences are larger than any overhead signal
we can extract. The cost figures above include the logging cost; the underlying
port cost is no higher than reported.

## Headroom and what it means for M2

- At 60 Hz the current ports use 0.65-0.70 ms of CPU and 0.125-0.139 ms of GPU
  per game step at 426x240. The step budget is 16.667 ms.
- At the 120 Hz paced model the cost is 1.52-1.54 ms CPU and 0.22-0.24 ms GPU
  per step; at 144 Hz it is 1.71-1.79 ms CPU and 0.26 ms GPU. The extra cost is
  mostly the additional clears and copies, not a new workload.
- The unlocked ceiling of about 14.5k presents/s shows the present path is not
  what limits 120 or 144 presents/s; the 60 Hz logic loop and its input sampling
  are the fixed part.
- This is headroom for the current 426x240 presentation only. The M2 renderer,
  shaders, upscaled assets and 2.5D lighting will add cost, and none of it is
  measured here. There is no claim that the planned renderer fits the budget.
- One run per configuration, one host, one driver stack. The numbers are a
  baseline, not a release benchmark.

## Gaps

- **No real 120/144 Hz display**: the host exposes only ~60 Hz modes; 1920x1080
  runs at 59.96 Hz. The 120/144 rows are a synthetic present cadence; scan-out,
  compositor frame pacing and the panel's own latency are not measured at those
  rates.
- **No scan-out measurement**: the 60 Hz input latency ends at the swap return.
  Compositor composition, scan-out and display latency are not included, and a
  photodiode or high-speed-camera measurement was not made.
- **Synthetic input**: the latency runs use a `uinput` keyboard, not a physical
  keyboard or gamepad. USB polling, physical debounce and gamepad latency are
  not measured. Twenty presses per case is a small sample; the p95 and max are
  indicative, not stable.
- **Scene control**: SA1 uses scripted gameplay, SA2 uses its recorded TAS tape,
  and the SA2 latency runs sit at the title/attract state. The ports are not
  compared in the same scene.
- **Single run per configuration**: no repeat-run variance study, no
  temperature or clock control. The clean-control comparison shows that
  run-to-run differences can exceed the effect being looked for.
- **No hardware counters**: `perf_event_paranoid=4`; CPU time is process-wide,
  with no per-thread or per-subsystem breakdown. GPU time is the `gfx` engine
  only; compute, copy and video engines are not included.
- **Global GPU is noisy**: `gpu_busy_percent` includes the compositor and other
  desktop clients. The per-process engine time is the cost figure; the global
  numbers are context only.
- **The 120 Hz latency rows were recorded separately from the 120 Hz pacing
  rows**: they use the same paced model but are not the same run, so the two
  120 Hz rows are not directly paired recordings.
- **Instrumentation overhead is not isolated**: the clean controls differ in
  scene or run conditions, so no correction is applied.
- **Resolution**: 426x240 client area, scale 1. The remaster's higher-resolution
  output is not measured.

## Reproduce

The raw logs and binaries are local and are not committed. From a checkout with
the local instrumentation and the user's ROMs:

```sh
# 60 Hz vsync, SA1 scripted scene
python3 tools/perf_run.py --binary /path/to/sa1-perf.sdl --tag sa1-60-vsync \
    --outdir /tmp/perf/sa1-60-vsync --seconds 42 --warmup 20 --cooldown 1 \
    --vsync 1 --pace 0 --script sa1

# 60 Hz vsync, SA2 TAS recorded tape
python3 tools/perf_run.py --binary /path/to/sa2-tas-perf.sdl --tag sa2tas-60-vsync \
    --outdir /tmp/perf/sa2tas-60-vsync --seconds 42 --warmup 20 --cooldown 1 \
    --vsync 1 --pace 0

# 120/144 Hz paced model: vsync off, synthetic cadence
python3 tools/perf_run.py --binary /path/to/sa1-perf.sdl --tag sa1-144-paced \
    --outdir /tmp/perf/sa1-144-paced --seconds 42 --warmup 20 --cooldown 1 \
    --vsync 0 --pace 144 --script sa1

# Analyse a run
python3 tools/perf_analyze.py --log /tmp/perf/sa1-60-vsync/sa1-60-vsync.log \
    --summary /tmp/perf/sa1-60-vsync/sa1-60-vsync.json \
    --from-step 1200 --to-step 2400
```

For a latency run, start the runner, wait until the scene is past loading and
the window has the compositor's focus, then inject and analyse:

```sh
python3 tools/perf_run.py --binary /path/to/sa2-perf.sdl --tag sa2-latency-60 \
    --outdir /tmp/perf/sa2-latency-60 --seconds 30 --warmup 3 --cooldown 1 \
    --vsync 1 --pace 0 &
sleep 12
python3 tools/perf_inject.py --window sa2 --key c --count 20 --interval 0.213 \
    --hold 0.04 --log /tmp/perf/sa2-latency-60/sa2-latency-60.inject.log
python3 tools/perf_analyze.py --log /tmp/perf/sa2-latency-60/sa2-latency-60.log \
    --summary /tmp/perf/sa2-latency-60/sa2-latency-60.json \
    --inject-log /tmp/perf/sa2-latency-60/sa2-latency-60.inject.log \
    --key-bit 1 --event-sym 99
```

The injector sets the X input focus and raises the window, but under Wayland the
compositor owns keyboard focus. Check that the game log contains `event` lines
before trusting a latency run; a run with zero events is a focus failure, not a
zero-latency result.

## Files

The committed summaries are generated from the local run logs and JSON
summaries; they contain no ROM, save, asset or binary data.

| File | Contents |
| :--- | :--- |
| [`docs/perf/baseline.csv`](perf/baseline.csv) | The eight 60/120/144 Hz baseline runs: pacing, cost and VRAM |
| [`docs/perf/latency.csv`](perf/latency.csv) | The six clean latency runs: event, sample and display stages |
| [`docs/perf/latency-stall.csv`](perf/latency-stall.csv) | The preserved SA2 60 Hz run with the 0.44 s input stall |
| [`docs/perf/clean-controls.csv`](perf/clean-controls.csv) | The clean-binaries comparison, not an overhead correction |
