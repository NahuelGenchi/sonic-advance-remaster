# Stage 0 host

This is the first runnable remaster host, not the remaster renderer. It proves
that the project can replace the upstream SDL platform while linking the
upstream game core from an external, pinned checkout:

- `tools/stage0/build.sh` validates the checkout, snapshots it outside this
  repository, builds the selected upstream SDL objects, archives the core
  without `src/platform/pret_sdl/sdl2.o`, compiles the host from
  `remaster/host/`, and links the executables. `SAR_GAME` selects `sa2`
  (default), `sa1`, or `both`.
- `remaster/host/` owns `main`, SDL video/input/audio, the save file,
  `VBlankIntrWait` and the presentation path. It creates an OpenGL 3.3 core
  context and draws either the upstream software PPU's `gameImage` at 426x240
  or the frame of the stage 1 layer path, with nearest sampling and integer
  letterboxing.
- `remaster/renderer/` holds the stage 1 layer path: the per-tick snapshot,
  the GLSL 330 layer rasterization and the composite pass. It is documented
  in [`../stage1/README.md`](../stage1/README.md) and the record of its
  verification is in [`../../docs/STAGE1.md`](../../docs/STAGE1.md).
- `remaster/renderer/` also holds the BG-sprite composition: the
  project-owned equivalent of the upstream functions that write the tilemap
  entries of the full-background pictures the game registers. The host calls
  it between the VBlank interrupt and the software PPU draw. Its rules and
  its recorded verification are in
  [`docs/BG-SPRITES.md`](../../docs/BG-SPRITES.md).
- The game still runs one 60 Hz tick per `VBlankIntrWait`; the host does not
  implement high-refresh interpolation, lighting or upscaled assets. Those are
  stages 2-4.

## Prerequisites

- A checkout of [`SAT-R/sa2`](https://github.com/SAT-R/sa2) at the commit in
  [`tools/upstream.pin`](../upstream.pin), outside this repository.
- SDL2 development files (`sdl2-config`), a C compiler, GNU make, `ar`, `tar`.
- An OpenGL 3.3 core driver and a working display for the windowed run.
- Optional: your own Sonic Advance 2 (USA) ROM. If `SA2_ROM` is set, the driver
  checks SHA-1 `7bcd6a07af7c894746fa28073fe0c0e34408022d` and copies it into the
  disposable snapshot as `baserom_sa2.gba`. The portable decomp build itself
  does not read the ROM.
- Optional: your own Sonic Advance (Europe) ROM. If `SA1_ROM` is set, the driver
  checks SHA-1 `eb00f101af23d728075ac2117e27ecd8a4b4c3e9` and copies it into the
  disposable snapshot as `baserom_sa1.gba`. The portable decomp build itself
  does not read the ROM.

## Build

From the repository root:

```sh
SA2_UPSTREAM=/tmp/sa2 \
SA2_ROM="/path/to/Sonic Advance 2 (USA) (En,Ja,Fr,De,Es,It).gba" \
SAR_BUILD_ROOT=/tmp/sar-stage0 \
tools/stage0/build.sh
```

For Sonic Advance 1, select the game and supply its ROM:

```sh
SAR_GAME=sa1 \
SA2_UPSTREAM=/tmp/sa2 \
SA1_ROM="/path/to/Sonic Advance (Europe) (En,Ja,Fr,De,Es).gba" \
SAR_BUILD_ROOT=/tmp/sar-stage0-sa1 \
tools/stage0/build.sh
```

`SAR_GAME=both` builds both cores from one snapshot and writes both binaries:

```sh
SAR_GAME=both \
SA2_UPSTREAM=/tmp/sa2 \
SA1_ROM="/path/to/Sonic Advance (Europe) (En,Ja,Fr,De,Es).gba" \
SA2_ROM="/path/to/Sonic Advance 2 (USA) (En,Ja,Fr,De,Es,It).gba" \
SAR_BUILD_ROOT=/tmp/sar-stage0-both \
tools/stage0/build.sh
```

The default build root is `${TMPDIR:-/tmp}/sar-stage0`; it is always outside the
repository. `SAR_TAS_TESTING=1` is the default and embeds the recorded Sonic
Advance 2 any% tape. Set `SAR_TAS_TESTING=0` for an interactive Sonic Advance 2
build where the keyboard drives the game. The Sonic Advance 1 target has no
tape and is always built interactive; `SAR_SCRIPT=sa1` replays its capture
schedule at run time.

Expected tail for the default (Sonic Advance 2) target:

```text
stage0: archiving ... sa2 upstream objects (sdl2.o excluded)
stage0: compiling the sa2 host
stage0: compiling the sa2 stage 1 layer path
stage0: linking the sa2 host against the core archive
stage0: ok; sa2 binary /tmp/sar-stage0/sa2-stage0
```

Everything is written under `SAR_BUILD_ROOT`: `source/` (the disposable
snapshot), `host/<game>/` (host objects), `lib<game>core.a`, `<game>-stage0`,
`upstream-build-<game>.log` and `provenance-<game>.txt`. A single-target build
also writes `upstream-build.log` and `provenance.txt` for compatibility. No
upstream file, ROM, save or build output is committed.

## Run

```sh
cd /tmp/sar-stage0
SAR_SAVE_PATH=/tmp/sar-stage0/sa2.sav ./sa2-stage0
```

For Sonic Advance 1:

```sh
cd /tmp/sar-stage0-sa1
SAR_SAVE_PATH=/tmp/sar-stage0-sa1/sa1.sav ./sa1-stage0
```

The window is 426x240 and resizable; Escape or closing the window exits. The
default key map is the upstream one: C = A, X = B, Return = Start, Backslash =
Select, S = L, D = R, arrows = D-pad.

## Verification helpers

These environment variables exist for recorded checks, not as a user-facing
feature:

| Variable | Meaning |
| :--- | :--- |
| `SAR_SAVE_PATH` | Save file path (default `sa2.sav` in the current directory). |
| `SAR_KEY_LOG` | Write key-state changes as `frame=N keys=XXXX`; used to prove injected or physical key presses reach `Platform_GetKeyInput`. |
| `SAR_CAPTURE_FRAME` | VBlank number to capture, or a comma separated list of them. Requires `SAR_CAPTURE_DIR`. |
| `SAR_RENDERER` | `software` (the default) draws the frame with the upstream software PPU, `gl` draws it with the stage 1 layer path. |
| `SAR_ALLOW_SOFTWARE_FALLBACK` | `1` makes a start-up failure of the GPU layer path fall back to the software PPU with a message. The default fails closed. |
| `SAR_PERF_LOG` | CSV with the per-frame cost of the layer path (snapshot, upload, submission, GPU). |
| `SAR_CAPTURE_DIR` | Existing directory for `frame_NNNNNN.ppm` (software PPU) and `frame_NNNNNN.gl.ppm` (GL back buffer). |
| `SAR_EXIT_AFTER_FRAME` | Exit cleanly after presenting this VBlank. |
| `SAR_VSYNC` | `1` (default) requests FIFO; `0` disables FIFO and uses the host's 60 Hz clock. Use `0` for recorded capture and audio runs so an occluded window cannot throttle the run. |
| `SAR_OAM_BRIDGE` | `0` disables the canonical OAM bridge. By default the host converts the GBA-native OAM records of the engine into the extended layout before the software PPU draws; see [docs/OAM.md](../../docs/OAM.md). |
| `SAR_SCRIPT` | `sa1` replays the Sonic Advance 1 capture input schedule instead of the keyboard, for deterministic recorded runs. |

A Sonic Advance 1 scripted frame check:

```sh
mkdir -p /tmp/sar-stage0-sa1/capture
SAR_CAPTURE_DIR=/tmp/sar-stage0-sa1/capture \
SAR_CAPTURE_FRAME=1400 \
SAR_EXIT_AFTER_FRAME=1400 \
SAR_SAVE_PATH=/tmp/sar-stage0-sa1/sa1.sav \
SAR_SCRIPT=sa1 \
SAR_VSYNC=0 \
SDL_VIDEODRIVER=x11 \
./sa1-stage0
```

A tape frame check:

```sh
mkdir -p /tmp/sar-stage0/capture
SAR_CAPTURE_DIR=/tmp/sar-stage0/capture \
SAR_CAPTURE_FRAME=1200 \
SAR_EXIT_AFTER_FRAME=1200 \
SAR_SAVE_PATH=/tmp/sar-stage0/sa2.sav \
SAR_VSYNC=0 \
SDL_VIDEODRIVER=x11 \
./sa2-stage0
```

Audio through SDL's disk driver:

```sh
SDL_AUDIODRIVER=disk \
SDL_DISKAUDIOFILE=/tmp/sar-stage0/audio.raw \
SAR_EXIT_AFTER_FRAME=900 \
SAR_SAVE_PATH=/tmp/sar-stage0/sa2.sav \
SAR_VSYNC=0 \
./sa2-stage0
```

## Fail-closed checks

The build stops if:

- `SA2_UPSTREAM` is unset, missing, or resolves inside this repository or any of
  its worktrees.
- The checkout is not a git work tree, its `HEAD` is not `tools/upstream.pin`,
  or it has tracked modifications.
- `SAR_BUILD_ROOT` is inside the upstream checkout, this repository, or any of
  its worktrees.
- `SAR_GAME` is not `sa1`, `sa2` or `both`.
- A required upstream file or the upstream build output for a selected game is
  missing.
- `SA1_ROM` is set but its SHA-1 is not the expected Europe ROM.
- `SA2_ROM` is set but its SHA-1 is not the expected USA ROM.
- A core archive defines `main`, or the host fails to compile or link.
- The build changes tracked files in the upstream checkout.

`-Wl,--no-undefined` makes a missing platform symbol a link failure instead of
a runtime surprise.

## What this does not establish

- No subpixel camera, interpolation, high-refresh schedule or frame pacing
  statistics.
- No lighting, depth/layer buffer or shader variants beyond the layer path
  and the present shader.
- The layer path is verified on the Leaf Forest Act 1 slice only; the affine
  background, colour effect, window and object window paths are implemented
  but the recorded scene does not exercise them.
- The Sonic Advance 1 target is built and run through the same host, but its
  own UI and rendering verification is limited to the recorded frames in
  [`docs/SA1-UI.md`](../../docs/SA1-UI.md) and [`docs/OAM.md`](../../docs/OAM.md).
- No packaging.
- The windowed run is Linux/X11 through XWayland on the recorded host; native
  Wayland, Windows and macOS were not checked.
