# Stage 1 GPU layer path

[Back to README](../README.md) · [Renderer architecture](RENDERER.md) · [Tracking](TRACKING.md) · [Stage 1 tools](../tools/stage1/README.md)

**Status:** the Stage 1 layer path is implemented and compared against the
upstream software PPU. It is the GPU layer path of the M2 vertical slice, not
the complete remaster renderer: there is no subpixel camera, no high-refresh
schedule, no lighting and no upscaled asset work in this stage.

## What this stage is

The stage 0 host presents the frame of the upstream software PPU. Stage 1
replaces that frame with a project-owned GPU layer path:

* a snapshot of the game state, captured once per tick at the point where the
  software PPU would start drawing;
* a rasterization of the GBA backgrounds and objects with OpenGL 3.3 core and
  GLSL 330;
* a composite pass that applies the priority, colour effect, window and
  object transparency rules of the software PPU;
* the present path, unchanged, which scales the composited scene to the
  window.

The upstream software PPU stays the reference and the fallback. The GPU path
never calls it, and it never writes game state.

The code is in [`remaster/renderer/`](../remaster/renderer) and
[`remaster/shaders/stage1_layers.h`](../remaster/shaders/stage1_layers.h); the
host integration is in [`remaster/host/`](../remaster/host). The tool
documentation, the switch and the debug aids are in
[`tools/stage1/README.md`](../tools/stage1/README.md).

## The switch

`SAR_RENDERER=software` (the default) keeps the software PPU.
`SAR_RENDERER=gl` selects the layer path. A failed context, a missing entry
point, a shader that does not compile or an incomplete framebuffer fails
closed with a message and exit status 1, before a frame is presented.
`SAR_ALLOW_SOFTWARE_FALLBACK=1` turns a start-up failure of the GPU path
into a fall back to the software PPU with a message on standard error.

## The snapshot

The snapshot is captured once per game tick, after the VBlank interrupt and
its DMA, at the same point where the software PPU would start drawing the
frame. The scanline state machine follows the order of the upstream PPU: set
`VCOUNT`, dispatch the VCount interrupt, reload the affine reference
counters, record the state of the line, run the HBlank DMA and the HBlank
interrupt, then step the affine references.

Recording the state per line is what makes the Leaf Forest BG3 effect
reproducible: the game copies four bytes into `BG3HOFS` and `BG3VOFS` on
every HBlank, and the layer path sees the same value on the same line as the
software PPU.

The VRAM is not copied per tick. The 120 KB of VRAM are split into 1 KB
pages, each page is hashed every tick and only the pages whose hash changed
are uploaded. The palettes are uploaded when their hash changes, the OAM and
the per-line register state are uploaded every tick.

## The tile walk

The upstream software PPU does not compute a map address per pixel. It walks
the map of a scanline tile by tile, restarts at the first map entry when the
map width is used up, and reads the row in groups of 32 tiles. The last
partial tile of a line is read from the position where the walk stopped,
which can be past the end of the map row.

`PpuTextTile` in the background shader reproduces that walk. Two details of
the widescreen build matter for it: the screen base of a text background has
six bits (`BGCNT_SCREENBASE_MASK` is `0x3F00` in this build), and the affine
path of the PPU still uses five bits.

## Verification

All runs use the pinned upstream checkout at
`393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d` and the deterministic
`TAS_TESTING=1` tape of Sonic Advance 2 (USA).

### Build

```sh
SA2_UPSTREAM=/home/rayzza/upstream/sa2 \
SAR_BUILD_ROOT=/home/rayzza/sar-builds/agent-04 \
SA2_ROM=".../Sonic Advance 2 (USA) (En,Ja,Fr,De,Es,It).gba" \
tools/stage0/build.sh
```

The driver validated the checkout, snapshotted it, built the upstream core
(520 objects, the SDL platform object excluded), compiled the host and the
stage 1 layer path, and linked `sa2-stage0` with `-Wl,--no-undefined`.

### The software path is unchanged

```sh
SAR_CAPTURE_DIR=<dir> SAR_CAPTURE_FRAME=1200 SAR_EXIT_AFTER_FRAME=1200 \
SAR_SAVE_PATH=<path> SAR_VSYNC=0 SDL_VIDEODRIVER=x11 ./sa2-stage0
```

The software frame 1200 has the SHA-256
`134e4df9b462e85f0e7a1a54727beec0c449454a5706d5f64a5f9c7692a81110`, the
same value as the stage 0 record, and the GL readback of that frame has the
same hash.

### The GPU path against the software PPU

Ten reference frames of Leaf Forest Act 1 were captured from the same tape
with both renderers and compared with
[`tools/stage1/compare_ppm.py`](../tools/stage1/compare_ppm.py).

| Frame | Differing pixels | Maximum channel delta |
| :---: | ---: | ---: |
| 800 | 0 | 0 |
| 950 | 0 | 0 |
| 1100 | 0 | 0 |
| 1200 | 0 | 0 |
| 1300 | 0 | 0 |
| 1450 | 0 | 0 |
| 1600 | 0 | 0 |
| 1800 | 0 | 0 |
| 2000 | 0 | 0 |
| 2150 | 0 | 0 |

The tolerance is zero: the two renderers agree on every channel of every
pixel of the ten frames. A wider sweep of the same act (every 100 frames
from 700 to 2300) is also identical on all seventeen frames.

The classes of difference that the stage had to reproduce, and how:

| Class | How it is reproduced |
| :--- | :--- |
| HBlank | The per-line state is recorded before the HBlank DMA of that line runs, exactly like the PPU order, so a register the game changes per line has the same value on the same line. |
| Tile walk | The map walk of the software PPU, including the 32 tile groups and the tail tile of a line, is reproduced in the background shader. |
| Screen base | The 426 pixel build uses a six bit screen base for text backgrounds; the layer path uses the same mask as the PPU. |
| Blend | The blend decisions are made on the integer 5-bit colour, with the same target bits as the PPU, and the result is converted to linear float afterwards. |
| Window | The window masks, the window priority and the window rectangle rules of the PPU are applied per pixel in the composite pass. |
| Sprite priority | The objects are drawn back to front (priority number first, then OAM index) and the composite pass orders the layers with the same key the PPU uses. |
| Filtering | The layer targets are nearest sampled and the present pass is nearest sampled, so no filtering difference is introduced. The linear light round trip of the scene target is exact for the 5-bit colour of this stage. |

### Tick rate

The host keeps its own 60 Hz clock when `SAR_VSYNC=0`. The recorded GPU runs
report 59.6 to 59.7 frames per second over 1200 and 2150 frame runs, that is
the 60 Hz game tick with the layer path in place.

### Cost

The layer path writes a per-frame cost line to `SAR_PERF_LOG`. A recorded
1200 frame run at 426x240 on the recorded host (AMD Radeon R9 380, Mesa
26.0.8) gives, in microseconds per frame:

| Stage | Mean | p50 | p99 | Max |
| :--- | ---: | ---: | ---: | ---: |
| Snapshot capture | 2.8 | 2.9 | 5.3 | 11.6 |
| Upload | 125.9 | 119.8 | 246.8 | 386.5 |
| Render submission | 2.0 | 1.8 | 6.7 | 15.6 |
| GPU (timer query) | 255.0 | 251.6 | 331.4 | 346.7 |

These are the numbers of this stage at the native resolution, not the
performance baseline of the renderer: the slice still has to measure the
presentation at 1080p and 4K, the high-refresh schedule and the lighting
stages. The upload is the largest CPU cost and is dominated by the VRAM
hashing; it can be replaced by a write hook when the game exposes one.

## What is not verified

* No frame of another zone, the act transition, the boss or the menus was
  compared in this repository; only Leaf Forest Act 1 was recorded.
* Affine backgrounds, alpha blending, brightness, the windows and the
  object window are implemented but the Leaf Forest slice does not exercise
  them, so there is no reference frame for them.
* Nothing was measured at a resolution other than the native 426x240.
* There is no subpixel camera, no high-refresh schedule and no lighting yet.

## Follow-up

* Replace the VRAM page hashing with a write hook when the upstream game can
  report its VRAM writes.
* Give the object layer the second frontmost pixel of the stack, so a semi
  transparent object blends against the object below it.
* Record the slice at 1080p and 4K and compare the composite cost with the
  targets of [`docs/RENDERER.md`](RENDERER.md).
