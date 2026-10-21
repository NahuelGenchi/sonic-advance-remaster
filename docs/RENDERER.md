# Renderer architecture

[Back to README](../README.md) · [Tracking](TRACKING.md) · [Frame capture](CAPTURE.md) · [BG sprites](BG-SPRITES.md) · [Issue #10](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/10)

**Status:** architecture decision for M2, with the stage 0 host and the stage 1
GPU layer path implemented. The host in `remaster/host/` replaces the upstream
SDL platform and creates a GL 3.3 core context. It presents either the
upstream software PPU frame at 426x240 (`SAR_RENDERER=software`, the default
and the reference) or the frame of the project-owned layer path
(`SAR_RENDERER=gl`). The layer path draws the GBA backgrounds and objects
from a per-tick snapshot and composites them with the priority, colour
effect, window and object transparency rules of the software PPU; on the ten
recorded Leaf Forest Act 1 reference frames it matches the software PPU on
every channel of every pixel. It is still not the remaster renderer: there is
no high-refresh presentation, no subpixel camera, no lighting and no upscaled
assets yet. The stage 1 record is [`docs/STAGE1.md`](STAGE1.md), the tools
are in [`tools/stage1/`](../tools/stage1/README.md), the integration harness
in [`tools/renderer_harness/`](../tools/renderer_harness/README.md) remains
the minimum boundary proof, and the stage 0 build driver is
[`tools/stage0/build.sh`](../tools/stage0/build.sh).

This document records the M2 architecture: the backend, the never-vendored
integration mechanism, the subpixel and high-refresh presentation model, the
shader stack, the 2.5D lighting depth model, performance targets, rejected
options, risks and the vertical-slice plan. Stage 0 was verified on 2026-10-19
against upstream `SAT-R/sa2` at
`393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d`; everything beyond that recorded run
is still a plan.

## Decision summary

| Area | Decision |
| :--- | :--- |
| Backend | SDL2 window, input and audio; OpenGL 3.3 core context; GLSL 330 shaders. |
| Integration | External `SAT-R/sa2` checkout at a pinned commit; a disposable build tree outside this repository; the project compiles its own host and links an upstream core archive. No upstream file, diff or asset is committed. |
| Scene source | The upstream software GBA PPU stays the reference and fallback. The remaster renderer consumes per-tick snapshots of VRAM, palettes, OAM, layer registers and HBlank scanline state. |
| Simulation | Unchanged 60 Hz game ticks. Presentation is decoupled and may run at 60, 120 or 144 Hz. |
| Subpixel camera | Presentation-only interpolation of camera, scroll, affine and OAM values between two ticks. Game logic keeps its integer camera and culling. |
| Lighting | A layer-ID/depth G-buffer feeds a depth-derived normal and a key/ambient/rim light. Depth is remaster-authored per zone, not read from the ROM. |
| Target | 1080p at 120 Hz and 4K at 60 Hz on a 2019-or-newer mid-range discrete GPU, with a 60 Hz software fallback. These are targets, not measurements. |
| Vertical slice | Sonic Advance 2 (USA), Leaf Forest Act 1, using the deterministic TAS tape. |

## What exists today

M1 established the baseline, and it is the starting point for every decision
here:

- The upstream SDL port renders **426x240** on desktop. `include/gba/defines.h:52`
  sets `DISPLAY_WIDTH 426` and `DISPLAY_HEIGHT 240` for portable, non-PSP,
  non-PS2 builds.
- `EngineMainLoop` in `src/core.c:547` runs one game tick and calls
  `VBlankIntrWait()` at `src/core.c:580`.
- The SDL platform implements `VBlankIntrWait` in
  `src/platform/pret_sdl/sdl2.c:296`. It runs an accumulator at
  `fixedTimestep = 1.0 / 60.0` (`sdl2.c:64`), calls `VDraw` (`sdl2.c:673`), and
  presents with an SDL renderer created with `SDL_RENDERER_PRESENTVSYNC`
  (`sdl2.c:220`).
- `VDraw` calls `gpsp_draw_frame(gameImage)` (`sdl2.c:675`), then uploads the
  16-bit ABGR1555 buffer to an SDL streaming texture.
- `gpsp_draw_frame` in
  `src/platform/shared/video/gpsp_renderer.cc:2286` is the software GBA PPU. It
  walks 240 scanlines, updates registers and HBlank state, and writes the final
  image into `gameImage`.
- `gCamera` is integer-valued: `CamCoord` is `s16` in SA1 and `s32` in SA2
  (`include/game/shared/stage/camera.h:7-11`). `UpdateCamera` derives it from the
  player's fixed-point position with `I(player->qWorldX)`
  (`src/game/shared/stage/camera.c:638-643`).
- Leaf Forest Act 1 already uses a per-scanline effect: `zone_1.inc.c:72-73`
  sets `gHBlankCopyTarget` to `REG_ADDR_BG3HOFS` and a 4-byte HBlank copy, and
  fills `gBgOffsetsHBlankPrimary` in `StageBgUpdate_Zone1Acts12`. A renderer
  that replaces the PPU must reproduce that scanline state; a simple
  full-screen quad cannot.

Stage 1 adds the GPU layer path on top of that host:
- `remaster/renderer/snapshot.c` runs the scanline state machine of the
  upstream software PPU and records the display state of every line, so the
  per-line HBlank effects of the game are reproduced. The VRAM is uploaded
  per dirty 1 KB page, the palettes when they change.
- `remaster/renderer/layer_path.c` rasterizes the four backgrounds, the
  objects and the composite pass with OpenGL 3.3 core and GLSL 330, and
  writes the composited scene in linear light.
- `SAR_RENDERER=software|gl` selects the renderer. A failed context, a
  failed shader or an incomplete framebuffer fails closed with a message;
  `SAR_ALLOW_SOFTWARE_FALLBACK=1` falls back to the software PPU.
- The ten recorded Leaf Forest Act 1 reference frames are identical to the
  software PPU frames, and so are fifteen further frames that cover the act
  transition, Leaf Forest Act 2 and the boss, which use the windows and the
  colour effects. The recorded runs hold the 60 Hz tick.

Stage 0 replaces the presentation owner without replacing the game:

- `tools/stage0/build.sh` validates the external checkout, snapshots it with
  `git archive`, builds the upstream `sdl` objects, archives every object except
  `src/platform/pret_sdl/sdl2.o`, and links the project host from
  `remaster/host/`.
- The host owns `main`, the SDL window and events, the keyboard state,
  `Platform_GetKeyInput`, `Platform_QueueAudio`, the save file,
  `VBlankIntrWait` and `Platform_StoreSaveFile`. It creates a GL 3.3 core
  context with `SDL_GL_CreateContext`, converts `gameImage` from ABGR1555 to
  RGBA8, uploads it to a texture, and draws one nearest-sampled quad at 426x240
  with integer letterboxing. The only shader is the present shader; there is no
  scene shader stack.
- The host also owns the first piece of renderer-owned game state: the
  BG-sprite composition of `remaster/renderer/`, which writes the tilemap
  entries of the full-background pictures the game registers with
  `DisplaySprite_BG`. It runs between the VBlank interrupt and the software
  PPU draw; its rules and its recorded verification are in
  [`docs/BG-SPRITES.md`](BG-SPRITES.md).
- The 60 Hz game tick is unchanged. Stage 0 paces presentation to 60 Hz and does
  not implement the decoupled high-refresh scheduler. `SAR_VSYNC=0` disables
  FIFO for deterministic recorded runs; the host then uses its own 60 Hz clock.
- The recorded run used Sonic Advance 2 (USA) and the `TAS_TESTING=1` tape.
  Tape frame 1200 matches the committed M1 capture exactly, and the GL back-buffer
  readback has the same SHA-256 as the software frame. The exact commands and
  outcomes belong in the stage 0 delivery record.

The upstream tree also contains an experimental OpenGL renderer for Win32
(`src/platform/win32/opengl.c`). It is Windows-only, fixed-function-era code,
not wired to the SDL port, and marked experimental. It is prior art, not a
foundation for this work.

## Backend choice

### Chosen: SDL2 + OpenGL 3.3 core

SDL2 is already the port's window, input and audio layer, so the remaster keeps
it and replaces only the draw path. OpenGL 3.3 core provides everything the
shader stack needs:

- programmable vertex and fragment shaders;
- framebuffer objects with multiple render targets;
- integer textures and integer samplers for VRAM/tile data;
- instanced drawing for OAM sprites;
- texture arrays for palettes and optional material data;
- floating-point render targets for lighting;
- broad availability on Linux and Windows. macOS exposes 3.2/4.1 core profiles
  rather than a 3.3 profile; it is a follow-up target, not part of the recorded
  M2 host.

The context is created with `SDL_GL_CreateContext`, requesting a 3.3 core
profile, double buffering and no depth buffer on the default framebuffer. Depth
and layer data live in an offscreen framebuffer. GLSL is pinned to `#version
330 core`; compute shaders (GL 4.3) and bindless features are not required.

GL entry points are resolved through `SDL_GL_GetProcAddress` behind a small
project-owned function table. If the table grows past roughly 60 entry points,
the implementation switches to a generated glad2 loader; the architecture does
not depend on which loader is used.

The default framebuffer only receives the final present pass. All scene work is
in linear-light `RGBA16F` targets, and the present pass applies the sRGB
transfer function. A simple exposure/tonemap stage is allowed; bloom and other
effects are optional and off in the vertical slice.

### Window and display

- `SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI`; drawable size comes from
  `SDL_GL_GetDrawableSize`, not the logical window size.
- Default presentation is **integer scale with letterboxing** for pixel-art
  clarity. A `fill` mode scales to the window and accepts uneven pixel sizes; a
  `smooth` mode uses linear filtering for the lighting/upscale path. The mode
  changes only the projection and sampling, never game state.
- Refresh rate is read from the current display mode. The host supports 60, 120
  and 144 Hz first; 75/90 Hz and VRR are follow-ups.
- Vsync is requested with `SDL_GL_SetSwapInterval(1)` (FIFO). The stage 0 host
  accepts `SAR_VSYNC=0` for recorded runs and uses its 60 Hz clock instead; a
  later CLI can expose the same as `--no-vsync`. `SDL_GL_SetSwapInterval(-1)`
  (adaptive) is optional and only used when the driver reports it.

## Never-vendored integration

### Invariants

1. No upstream source, diff, asset, binary or generated extraction enters a
   commit in this repository.
2. `SAT-R/sa2` is never forked, submoduled or modified. Upstream has no license
   and a strict no-LLM contributions policy; nothing produced here is proposed
   back.
3. The build reads an external checkout and never writes to it. Build output
   goes to the git-ignored `build/` directory for the harness, or to an external
   `SAR_BUILD_ROOT` for a full port build; it never includes upstream source.
   The harness additionally proves the checkout's tracked files are unchanged
   after a run.
4. The upstream commit is pinned in [`tools/upstream.pin`](../tools/upstream.pin)
   and in this document.
5. The remaster layer is this repository's own code. It is compiled against the
   upstream headers, not copied into the upstream tree.

### Layout

```text
this repository                            external, never committed
sonic-advance-remaster/                    /path/to/sa2/
├── docs/RENDERER.md                       ├── .git/
├── remaster/                              ├── src/
│   ├── host/                              ├── include/
│   ├── renderer/  (stage 1+)              └── ...
│   └── shaders/   (stage 1+)
├── tools/
│   ├── upstream.pin
│   ├── renderer_harness/
│   └── stage0/
└── build/  (git-ignored objects)          /path/to/build-root/
                                           ├── source/          clean snapshot
                                           ├── host/<game>/     host objects
                                           ├── lib<game>core.a  game core
                                           ├── <game>-stage0    host binary
                                           └── provenance-<game>.txt
```

### Build stages

The stage 0 driver implements steps 1-7 for the selected game: `sa2` by
default, `sa1`, or `both` from one snapshot. The harness keeps the same boundary
at minimum scale.

1. **Validate.** `SA2_UPSTREAM` must resolve to a git work tree outside this
   repository, `HEAD` must equal the pin, and tracked files must be clean. A
   missing, wrong-revision or tracked-modified checkout fails closed; untracked
   files are ignored. There is no fallback to a vendored copy.
2. **Snapshot.** `git archive <pin>` from the external checkout is extracted to
   an external `SAR_BUILD_ROOT/source`, outside this repository. This produces a
   clean upstream tree without `.git` and never writes to `SA2_UPSTREAM`. ROMs
   are copied into that snapshot only for the build; they stay outside every
   commit.
3. **Build the reference port.** `make sdl GAME_NAME=sa2` (and `sa1` for the
   second port) in the snapshot produces the upstream objects and a reference
   binary. Stage 0 passes `TAS_TESTING=1` for the SA2 slice build and always
   builds SA1 interactive. This is the same build documented in
   `docs/CAPTURE.md`, with the same pinned commit and the user's own ROM; the
   portable decomp build does not read the ROM, but the driver validates it when
   `SA1_ROM` or `SA2_ROM` is supplied.
4. **Archive the core.** `ar` collects the upstream objects except
   `src/platform/pret_sdl/sdl2.o`. The archive is the unmodified game logic,
   software PPU and shared platform helpers. The platform object is the only
   piece the remaster host replaces.
5. **Compile the host.** The project's `remaster/` sources are compiled from
   this repository against the snapshot's `include/` directory, into the build
   root. Nothing from `remaster/` is copied into the upstream tree.
6. **Link.** The host executable is linked from the host objects, the core
   archive, `libagbsyscall` and SDL2, with `-lm`. GL entry points are loaded
   through `SDL_GL_GetProcAddress`, so the stage 0 binary does not link `libGL`
   directly. `-Wl,--no-undefined` is used so a missing platform symbol fails at
   link time, not at runtime.
7. **Run.** The host owns `main`, the window, the GL context, input, audio and
   save-file plumbing. It calls `AgbMain` from the core archive and provides
   `VBlankIntrWait`, `Platform_GetKeyInput`, `Platform_QueueAudio` and
   `Platform_StoreSaveFile` itself.

The compatibility path keeps `sdl2.o` and wraps `gpsp_draw_frame` with
`-Wl,--wrap=gpsp_draw_frame`. That is useful for A/B comparisons between the
upstream SDL presentation and an overlay. It is not the production seam,
because GNU-style `--wrap` is not available in the same form on every linker and
because the upstream SDL renderer still owns the window. The production seam is
the core archive plus host link, which works with a normal linker; the harness
demonstrates that archive/host link, not the wrap path.

### Why not patch upstream

A patch series would be smaller to start with, but it would carry upstream
context and diffs into this repository and would drift with every upstream
commit. It would also make the build depend on applying text patches to an
unlicensed tree that must never be redistributed. New files only, compiled from
this repository, keep the boundary clean and make a failed upstream change a
build error instead of a merge conflict.

### Revision updates

A pin update is its own work item. It changes `tools/upstream.pin`, re-runs the
harness, re-runs the full build, and records any changed upstream symbols or
behaviour. It is not mixed with renderer changes.

## Subpixel camera and resolution

### What "subpixel" means here

The game's camera and culling stay integer. Subpixel is a property of the
presentation transform:

- **Temporal subpixel.** At refresh rates above 60 Hz, the host interpolates
  between two adjacent 60 Hz snapshots. A camera that moved 3 native pixels in
  one tick is presented at 1.5 and 3.0 native pixels on the intermediate
  frames, so motion is smooth instead of stepping.
- **Spatial subpixel.** The scene is projected from the 426x240 native world to
  the drawable size with a fractional scale. Geometry and UVs are floating
  point; the art can be sampled nearest (pixel-perfect presets) or linear
  (smooth presets).

Neither changes `gCamera`, OAM coordinates, scroll registers, culling, physics
or RNG. The renderer reads snapshots; it does not write game state.

### Snapshot

At each game tick, before the VBlank callback can change registers, the host
captures:

| Data | Size | Notes |
| :--- | :--- | :--- |
| Camera `x`, `y`, shift, shake | small | Copied for depth and diagnostics; not required for scroll because registers already contain it. |
| BG scroll and affine registers | 4 layers | `HOFS`, `VOFS`, `PA..PD`, reference X/Y, `BGxCNT`. |
| Per-scanline BG scroll | 4 x 240 x 2 x `u16` | Captured after HBlank DMA and HBlank callbacks, in PPU order. |
| OAM | 128 entries | Position, shape, size, tile, palette, priority, affine, mosaic, mode. |
| Palettes | 512 x `u16` | Uploaded when the palette changes. |
| VRAM dirty ranges | variable | Uploaded when the game writes VRAM; never a full VRAM copy per tick. |
| Blend, window, mosaic, display control | small | `DISPCNT`, `BLDCNT`, `BLDALPHA`, `BLDY`, `WIN0/1`, `WININ`, `WINOUT`, `MOSAIC`. |

The HBlank capture is the part that must be exact. The host runs the scanline
state machine, not the software pixel loop: set `REG_VCOUNT`, step affine
references, call `RunDMAs(DMA_HBLANK)`, invoke the HBlank interrupt, then record
the registers for that line. The order matches
`gpsp_renderer.cc:2286-2325`: reload counters, render line, HBlank DMA, HBlank
interrupt. The software PPU is used as the parity reference, not called twice
per tick.

### Interpolation

At presentation time, with `alpha` in `[0, 1]` between snapshots `N-1` and `N`:

```text
present = prev + (curr - prev) * alpha
```

- **Scroll registers:** interpolate on the hardware wrap width (10 bits for
  text backgrounds, affine reference width for affine layers). A delta larger
  than half the wrap is treated as a wrap, not a jump.
- **OAM positions:** interpolate X (9 bits) and Y (8 bits) with the same
  shortest-delta rule. A teleport, respawn or off-screen wrap larger than a
  threshold snaps to the current value instead of sliding across the screen.
- **Affine matrices:** interpolate `PA..PD` and the reference point linearly.
  Large changes snap. This is exact for translation and small scale/rotation
  changes; it is not a rotation slerp and does not need to be for GBA effects.
- **Tiles, palettes, sizes, priorities, blend/window state:** use the current
  snapshot. They are not meaningful to interpolate and changing them mid-tick
  would invent art that never existed.
- **Per-scanline scroll:** interpolate row by row when the row delta is small;
  otherwise use the current row. Water and heat-haze effects can change a row
  abruptly, and sliding those rows would look wrong.

The default interpolated presentation intentionally lags the newest snapshot by
up to one game tick. That is the standard fixed-timestep interpolation
trade-off: it buys smoothness at 120/144 Hz at the cost of up to 16.7 ms. A
`--present-latest` mode presents the newest snapshot without interpolation for
latency-sensitive testing. The default is chosen by measurement in the slice,
not by assumption.

At 60 Hz there is no intermediate frame; the renderer presents the current
snapshot and the temporal benefit disappears. Spatial subpixel scaling still
applies.

### Resolution and projection

The native world is 426x240. The renderer's scene targets are sized to the
drawable size (or a quality-preset cap), and the projection is:

```text
scale = min(drawableW / 426, drawableH / 240)   // fit
offsetX = (drawableW - 426 * scale) / 2
offsetY = (drawableH - 240 * scale) / 2
```

- `pixel-perfect`: `scale = max(1, floor(scale))`; bars fill the remainder.
- `fill`: fractional `scale`; nearest sampling.
- `smooth`: fractional `scale`; linear sampling in the lighting/upscale pass.

No layer or sprite is culled at 240x160. The widescreen hack already extends
VRAM and OAM (`include/gba/defines.h:58-63`); the renderer culls at 426x240 plus
a margin so sprites entering from the sides are not popped.

## High-refresh presentation

### Tick and present are separate clocks

The game remains a 60 Hz fixed-step simulation. The host's `VBlankIntrWait`
becomes the presentation window:

```text
VBlankIntrWait():
    capture snapshot N
    run the VBlank interrupt once
    present interpolated frames until the next 60 Hz deadline
    return to the game loop
```

The game then prepares the next frame's VRAM and OAM, exactly as upstream does
after `VBlankIntrWait` returns. At 120 Hz the host presents twice per tick; at
144 Hz it presents two or three times, depending on phase. The scheduler uses a
monotonic clock and absolute deadlines, not `SDL_Delay` accumulation.

- A late present is skipped, not queued.
- A missed tick is caught up at most once per call, so a stall cannot spiral.
- Audio generation stays at one `m4aSoundMain` per tick. The SDL audio queue is
  not stretched or resampled to the render rate.
- Input is polled at the display rate and latched into the next tick's
  `REG_KEYINPUT`, so a short press between ticks is not lost. The game still
  reads one input state per tick.

### Vsync and pacing

- Default: FIFO vsync through `SDL_GL_SwapWindow`. The scheduler targets the
  display's refresh interval and reports the interval distribution.
- No-vsync: the same schedule with a sleep/spin deadline; useful for measuring
  render cost without driver blocking.
- VRR: follow-up. It needs a frame limiter and a different present policy; it is
  not assumed to work on the current 59.96 Hz host.

The host reports, behind a debug flag: tick rate, present rate, present interval
p50/p99, missed deadlines, late ticks, and CPU/GPU frame time. The format is
specified by the slice implementation; the numbers come from recorded runs.

### Render thread

The vertical slice presents synchronously inside `VBlankIntrWait`. The game is
blocked there anyway, and a single thread keeps snapshot ownership simple. If
the slice shows that GPU submission or driver waits steal tick time, the
snapshot boundary already supports moving presentation to a render thread with
double-buffered snapshots. That is a measured decision, not a default.

## Shader stack

All shaders are GLSL 330 core. The scene is rendered in this order:

| Pass | Input | Output | Purpose |
| :--- | :--- | :--- | :--- |
| 1. Layer raster | VRAM, palettes, tilemaps, scroll/affine, OAM | layer colour `RGBA16F`, coverage, object/depth ID | Decode text, affine and bitmap backgrounds; decode 16/256-colour sprites and affine sprites. |
| 2. GBA composite | layer colour, blend/window/mosaic registers, scene colour, scene layer ID | scene colour `RGBA16F`, depth `R32F`, layer ID `R8UI` | Apply GBA priority, alpha/brightness blend, windows and OBJ semi-transparency. |
| 3. Lighting | scene colour, depth, layer ID | lit colour `RGBA16F` | Reconstruct normals, apply key/ambient/rim, optional material response. |
| 4. Present | lit colour, drawable size | default framebuffer | Scale, letterbox, sRGB encode, exposure/tonemap, optional scanline/CRT overlay. |

### Layer raster

- **Text backgrounds:** 256x256, 512x256, 256x512 and 512x512 maps; 16- and
  256-colour tiles; horizontal/vertical flips; per-scanline `HOFS`/`VOFS`.
  Tilemap indices come from an `R16UI` texture; tile pixels from an `R8UI`
  texture; colours from a palette texture. Mosaic quantises the sample
  coordinate.
- **Affine backgrounds:** `PA..PD` and reference registers; wrap modes; 16/256
  colour; per-scanline offsets where the game writes them. Affine reference
  stepping is part of the scanline capture, not a per-fragment guess.
- **Bitmap modes:** modes 3, 4 and 5 are supported by sampling the bitmap area
  of VRAM. They are lower priority than modes 0-2 for the slice because Leaf
  Forest does not use them, but the shader variant exists in the design.
- **Objects:** instanced quads, one instance per OAM entry, with shape/size,
  tile, palette, flips, affine matrix, priority, mosaic and OBJ mode. 16- and
  256-colour variants. OBJ window mode writes an object mask for the composite
  pass.
- **Widescreen:** samples at X up to 426; no 240-wide clamp. The tilemap wrap
  and VRAM extended area follow the upstream widescreen layout.

Texture filtering is nearest for palette/tile data. Layer targets are point
sampled when composited; the lighting pass can use linear sampling for the
depth-derived normals. The present pass chooses nearest or linear by preset.

### GBA composite

The composite pass must reproduce the PPU's ordering and blending, not a
modern alpha-over approximation. It uses integer GBA semantics for the blend
decision, then converts to linear float:

- Priority order: lower number wins; OBJ and BG tie-breaks follow the software
  PPU (`gpsp_renderer.cc`, `order_layers` and `order_obj`). The slice validates
  ties against the reference renderer.
- `BLDCNT` first/second target selection; `BLDALPHA` 5-bit weights; `BLDY`
  brightness up/down. The destination layer ID is needed, which is why the
  composite keeps one.
- Windows 0/1 and the OBJ window; `WININ`/`WINOUT` enable bits; outside-window
  behaviour.
- Mosaic is applied in the layer raster, not in the composite, so the sampled
  art matches the PPU.
- Forced blank and display-off paths are explicit.

The pipeline renders layers back-to-front. Each layer pass writes into the
scene target and updates the layer/depth targets only where it is visible.
Alpha-blended pixels keep the top layer's depth and ID for lighting; the colour
already contains the blend. This is a deliberate approximation for lighting, not
for colour: colour blending is exact, while lighting a blended pixel uses the
front layer's depth.

The exact number of layer targets is an implementation detail. The design
allows one layer texture reused per pass, plus the scene target, depth target
and layer-ID target. At 1080p that is roughly 16.6 MB per `RGBA16F` target and
8.3 MB per `R32F` target (8.3 M pixels at 4K would be four times that); the
slice measures the real footprint and caps the internal render scale if 4K
targets exceed the memory budget.

### Lighting pass

The lighting pass is a full-screen shader over the scene colour, depth and layer
ID. It reconstructs a normal from the depth gradient, applies the zone's light
rig, and writes a lit colour. It does not change geometry, silhouettes or
palette hues beyond the configured light response. Lighting can be disabled at
runtime for A/B review.

### Present pass

The present pass maps the scene target to the drawable rectangle, applies the
selected filtering, converts linear colour to sRGB, and optionally applies
exposure/tonemap and a subtle scanline/CRT effect. The effect is off by default
and is not part of the M2 acceptance criteria; it must never be the only way the
image looks correct.

### Shader compilation

Shader variants are compiled at startup from `remaster/shaders/` and cached in
memory. The permutation set is small: text/affine/bitmap background, 16/256
colour, normal/affine OBJ, window/mosaic flags, lighting on/off. Runtime shader
compilation during gameplay is not allowed; a missing variant is a startup
error. Binary shader caching is optional and driver-specific.

## 2.5D lighting depth model

### What the ROM does and does not contain

The GBA provides draw order, not physical depth. `BGxCNT` priority and OAM
priority say which pixel is in front; they do not say how far away a layer is.
The remaster therefore authors depth per zone. That is a presentation decision
and does not change collision, physics or layering in the game.

### Depth sources

| Source | Depth rule | Confidence |
| :--- | :--- | :--- |
| Backdrop | `1.0` | Certain. |
| Text BG layers | Per-zone table, default from priority: BG3 `0.90`, BG2 `0.70`, BG1 `0.50`, BG0 `0.35` | Authored; validated by eye. |
| Affine/parallax BG layers | `depth = clamp(1 / parallaxFactor, 0.2, 1.0)`, where `parallaxFactor` is measured from scroll delta over camera delta; fall back to the table when the camera is still | Estimated, then authored. |
| Per-scanline BG layers | Optional `depthTop`/`depthBottom` from the zone table; interpolate by screen Y | Authored for floors/ceilings. |
| Objects | `objBase + (1 - screenY / 240) * objRange + priority * 0.02`; optional per-sprite override keyed by tile/palette or OAM index | Estimated; authored for hero/boss. |

A zone file (for example `remaster/zones/leaf_forest.json`) holds the layer
table, key light direction/colour, ambient colour, rim strength, exposure and
any per-sprite overrides. It contains no ROM data: only floats and identifiers
that the renderer resolves against the running game's tiles.

### Normals

Normals are reconstructed from the depth buffer in screen space:

```text
d  = depth at uv
n  = normalize(vec3(dFdx(d) * strength, dFdy(d) * strength, 1.0))
```

`strength` is per layer: zero for flat backgrounds, moderate for sprites, and
higher for objects that should read as rounded. A depth discontinuity at a
sprite edge produces a bevel; the strength controls how much. The slice may add
authored normal maps for upscaled sprite tiles later, keyed by tile/palette;
they are not required for the first lighting result.

### Light rig

The base rig is one directional key light, a cool ambient term, and a rim term:

```text
N     = depth normal
L     = normalize(keyDirection)
V     = (0, 0, 1)
H     = normalize(L + V)
diff  = saturate(dot(N, L))
spec  = pow(saturate(dot(N, H)), shininess)     // sprites only, optional
rim   = pow(1 - saturate(dot(N, V)), rimPower)  // sprites only
light = ambient + keyColour * diff
out   = sceneColour * light + keyColour * spec * specStrength + rimColour * rim
```

The exact values are per zone and tuned in the slice. The art direction rule is
that lighting must not reduce readability: no strong specular on backgrounds, no
normal map that changes a silhouette, and a visible toggle to compare against
the unlit frame.

Depth-derived lighting is a stylised 2.5D effect, not a physically based
renderer. The document deliberately avoids claiming contact shadows, global
illumination or per-material PBR in M2. A cheap contact shadow or a screen-space
ambient occlusion pass can be evaluated later if the slice has budget.

## Performance targets

These are budgets for the vertical slice, not measurements. The baseline host
and its headroom are being recorded by issue #9; the slice records the actual
hardware, driver and numbers.

| Area | Target | How it is checked |
| :--- | :--- | :--- |
| Simulation | 60.00 Hz; no dropped ticks over a 60 s scripted run | Tick log and state hash. |
| Tick CPU | p99 at most 8 ms, leaving render headroom | Timestamps around the game tick. |
| Present, 1080p120 | GPU frame p99 at most 8.33 ms | GL timer queries and an external frame-time capture. |
| Present, 1080p60 | GPU frame p99 at most 16.67 ms | Same. |
| Present, 4K60 | GPU frame p99 at most 16.67 ms on the reference class | Same, with the 4K preset. |
| Snapshot/upload | At most 1 ms per tick; HBlank capture at most 0.5 ms | CPU timers around capture and upload. |
| GPU memory | At most 256 MB at 1080p, 512 MB at 4K | Driver/API query and a target-size calculation. |
| Input latency | At most two display refreshes plus one game tick, from poll to visible present | Synthetic timestamp and high-speed capture where available; #9 defines the method. |
| Determinism | State hash at tick N identical at 60, 120 and 144 Hz with the same input | Scripted run comparison. |
| Warm-up | No shader compile after the first presented frame; no hitch over 50 ms after warm-up | Startup log and frame-time log. |
| Fallback | Software PPU path remains usable at 60 Hz if GL 3.3 or the shader stack fails | Forced-fallback run. |

The reference class is a discrete GPU from 2019 or later with at least 4 GB of
VRAM and an OpenGL 4.5 driver. Naming a specific model in the slice report is
allowed only after that model is measured. Integrated GPUs get a reduced
internal render scale and are not promised the 4K target.

## Rejected options

| Option | Why it was rejected | Revisit when |
| :--- | :--- | :--- |
| Keep `SDL_Renderer` and add effects around it | SDL2's renderer does not expose custom shaders or the depth/layer targets the lighting model needs. | Never for this design. |
| SDL3 + `SDL_GPU` | Attractive cross-backend API, but it would replace the project's SDL2 dependency and platform layer before the renderer exists. | After the M2 slice, as a separate migration. |
| Vulkan renderer | More explicit control, but much more code, more driver variance and a harder macOS path. The scene is small; GL 3.3 is sufficient. | If GL becomes a measured bottleneck or a target platform drops it. |
| bgfx / raylib / a game engine | Large dependency, own asset/window model and license surface; the project needs a thin layer over existing SDL2 and upstream code. | Only with a concrete feature that cannot be built directly. |
| Reuse the upstream Win32 OpenGL renderer | Windows-only, experimental, fixed-function-era, not wired to the SDL port, no lighting/depth model. | As reference only. |
| Patch, fork or submodule upstream | Vendors an unlicensed tree or its diffs, drifts with every update, and conflicts with the no-upstream-contribution rule. | Never. |
| `LD_PRELOAD` / runtime injection | Not a shipping build, hard to debug, and hides the boundary. | Never. |
| Software PPU at 4K | CPU cost scales with pixels; no shaders or lighting; defeats the milestone. | Never as the primary path; keep as fallback/reference. |
| Run game logic at 120/144 Hz | Changes physics, timers and determinism; the game is authored for 60 Hz. | Never. |
| Full 3D remake | Changes the game and the art direction; this is a remaster, not a remake. | Out of scope. |
| Frame generation / temporal upscaling | Artifacts on pixel art, extra latency; not needed at this scene complexity. | After M3, if at all. |
| CPU per-pixel lighting | Too slow at 1080p/4K and duplicates the GPU path. | Never. |
| Pre-rendered upscaled backgrounds as the primary scene | Loses dynamic layers, HBlank effects and lighting; asset licensing/provenance risk. | Optional upscaled asset pack later, on top of the renderer. |

## Risks and mitigations

| Risk | Impact | Mitigation |
| :--- | :--- | :--- |
| HBlank/affine parity is wrong | High: water, clouds and split backgrounds look wrong; some zones break. | Capture scanline state once per tick in PPU order; compare against the software PPU; support per-layer fallback; make HBlank a slice exit criterion. |
| Platform replacement misses a symbol or behaviour | High: link failure or runtime crash in input/audio/save. | `-Wl,--no-undefined`; incremental host with the harness boundary; reuse upstream shared platform helpers; test both ports. |
| Upstream pin drifts | Medium: build breaks or behaviour changes silently. | Pin file, fail-closed harness, dedicated update issue, re-run parity and performance checks. |
| OpenGL driver/context variance | Medium: black screen, wrong filtering, vsync differences. | Capability checks, clear error path, software fallback, test Linux first. |
| High refresh unavailable or uneven | Medium: the headline feature cannot be shown on the host. | Support 60 Hz, detect refresh, record the host limitation, use paced-present experiments; do not claim a display mode that was not present. |
| Interpolation adds latency or artifacts | Medium: controls feel worse; teleports smear. | One-tick interpolation default, snap on large deltas, `--present-latest` mode, measure with #9. |
| Visual regressions in pixel art | High: the remaster stops looking like the game. | Golden-frame comparison, lighting toggle, art review, no silhouette-changing normal maps. |
| Performance on integrated GPUs | Medium: unplayable on low-end hardware. | Quality presets, internal render scale, 60 Hz fallback, measure before promising. |
| GPU memory at 4K | Medium: allocation failure or swapping. | Render-scale cap, transient target reuse, measure actual footprint. |
| Depth model looks arbitrary | High: 2.5D lighting reads as a filter, not depth. | Per-zone authored tables, parallax-derived depth, A/B review, keep the effect subtle. |
| Save/audio portability | Medium: host replacement breaks saves or sound. | Reuse upstream `cgb_audio` and flash code; test save round-trip and audio stream; do not change the game's 60 Hz audio cadence. |
| Shader compile hitch | Low/medium: startup stall. | Compile all variants before the first frame; fail on missing variant; optional binary cache. |
| Legal/licensing | High if violated. | No upstream source/diff/asset committed; GPL-compatible dependencies; no upstream contributions; user-supplied ROMs only. |
| Scope creep into all zones/assets | High: M2 never lands. | Leaf Forest Act 1 slice only; other zones and upscaled assets are follow-up issues. |

## Vertical slice

### Target

**Sonic Advance 2 (USA), Leaf Forest Act 1**, run with the deterministic
`TAS_TESTING=1` tape already used for the M1 captures. Leaf Forest exercises
widescreen, parallax, sprites and a per-scanline BG3 effect, and the existing
frame at tape frame 1200 gives a fixed comparison point. Sonic Advance 1 follows
after the SA2 slice; its `s16` camera and different engine timing are not mixed
into the first slice.

### Stages

| Stage | Deliverable | Exit check |
| :--- | :--- | :--- |
| 0. Boundary and host scaffold | External build driver, core archive, host window/GL context, software-frame upload and present, input/audio/save stubs. | The host shows the same frame as the M1 port at 426x240 for a recorded run; no ROM or upstream file committed. |
| 1. GPU layer path | BG text/affine, OBJ, widescreen, alpha/brightness, HBlank BG3 scanline state, mode coverage used by Leaf Forest. | Ten reference frames match the software PPU within an explicit tolerance; every difference is explained. |
| 2. Subpixel and high refresh | Snapshot double buffer, interpolation, 60/120/144 schedules, input latch, pacing stats. | State hash at tick N is identical at all three rates; present intervals meet the slice budget or the shortfall is recorded. |
| 3. Lighting | Depth/layer G-buffer, normal reconstruction, key/ambient/rim, Leaf Forest depth table and light rig. | Lighting on/off A/B is reviewed; no silhouette or readability regression; the effect can be disabled. |
| 4. Performance and fallback | Quality presets, 1080p/4K runs, integrated-GPU check, software fallback. | Targets in this document are met on the recorded host or the gap and cause are documented. |

Stage 0's exit check is recorded in the stage 0 delivery record: the tape frame
matches the M1 capture and the host window, input and audio paths were run.
Stages 1-4 remain open, so the renderer itself is still a plan.

### Slice non-goals

- All zones, bosses, menus, special stages and multiplayer.
- Sonic Advance 1's port.
- Upscaled asset packs, normal-map authoring at scale, and any Sega asset in
  the repository.
- Windows/macOS packaging, release builds, controller remapping and save-state
  UI.
- Bloom, depth of field, contact shadows, ray tracing or frame generation.

### What the slice must record

- Exact upstream commit, host, driver, compiler and build commands.
- The tick/present logs and the state-hash comparison.
- The reference frame numbers and the comparison method/tolerance.
- The performance numbers and the GPU memory footprint.
- What was not measured and why.

## Open questions

These are deliberately unresolved until the slice has data:

1. Exact HBlank capture order for affine layers and mosaic; validate against
   the software PPU before trusting the GPU result.
2. Whether the first slice needs an OBJ-window mask or can defer windows until
   menus are in scope.
3. Whether synchronous presentation inside `VBlankIntrWait` stays within the
   tick budget, or a render thread is required. Stage 0 uses synchronous present
   at 60 Hz; that says nothing about the stage 1 GPU path.
4. The GL loader beyond stage 0: the small SDL2 function table is enough for the
   present shader, but the stage 1 stack may justify generated glad2.
5. The default interpolation latency mode after #9 measures input-to-present.
6. The zone depth-file format and who authors the per-sprite overrides.
7. Whether upscaled assets are a runtime replacement pack or a build-time
   pipeline, and how their provenance is recorded without committing Sega art.
8. The Windows and macOS link/presentation path; Linux is the only recorded
   target today.

## Verification record

The integration boundary is checked by:

```sh
SA2_UPSTREAM=/tmp/sa2 tools/renderer_harness/build.sh
```

The harness compiles `src/platform/shared/common.c` and
`src/platform/shared/video/gpsp_renderer.cc` from the external checkout,
archives them, links `tools/renderer_harness/probe.c` against the archive, and
runs the result. On 2026-10-19, with upstream at the pin and gcc/g++ 15.2.0, it
printed:

```text
harness: upstream software PPU 426x240
harness: upstream interrupt dispatch ok
harness: forced-blank checksum 5873dfc0
harness: first=ffff last=ffff
```

This is a compile/link/run check of the boundary and the software PPU, not a
game renderer, a game frame or a performance measurement.

The stage 0 host is checked by:

```sh
SA2_UPSTREAM=/tmp/sa2 \
SA2_ROM="$SA2_ROM" \
SAR_BUILD_ROOT=/tmp/sar-stage0 \
tools/stage0/build.sh
```

On 2026-10-19, with upstream at the pin, gcc 15.2.0, SDL2 2.32.10 and Mesa
26.0.8 on the recorded Linux/X11 host, the driver validated the checkout,
snapshotted it, built the upstream port, archived 520 core objects while
excluding `platform/pret_sdl/sdl2.o`, and linked `sa2-stage0` with no missing
symbols. The checkout's tracked files were unchanged after the build.

The tape run used `SAR_VSYNC=0` so the host paced itself to 60 Hz instead of
blocking on FIFO:

```sh
SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy SAR_VSYNC=0 \
SAR_CAPTURE_DIR=/tmp/sar-stage0/capture SAR_CAPTURE_FRAME=1200 \
SAR_EXIT_AFTER_FRAME=1200 SAR_SAVE_PATH=/tmp/sar-stage0/sa2.sav \
/tmp/sar-stage0/sa2-stage0
```

The window reported `426x240` through `xwininfo`, and the run captured
`frame_001200.ppm` and `frame_001200.gl.ppm`. Downscaling
`docs/images/sa2-leaf-forest.png` from 852x480 to 426x240 with nearest-neighbour
sampling and comparing both captures gave 0 differing pixels out of 102,240;
both captures have SHA-256
`134e4df9b462e85f0e7a1a54727beec0c449454a5706d5f64a5f9c7692a81110`. The GL
readback matching the software frame is evidence for the upload/present path,
not for a new renderer.

Keyboard injection into the focused X11 window produced `keys=0008` (Return)
and `keys=0001` (C) changes in the host key log, which is written by
`Platform_GetKeyInput`. A 900-tick run with SDL's disk audio driver
produced 15.55 s of audio with peak 5026, RMS 883.88 and 83.1% non-zero samples.

The stage 1 layer path was checked on 2026-10-20 with the same pinned
upstream checkout and the same tape. The driver built the host and the layer
path, the software frame 1200 kept its SHA-256
`134e4df9b462e85f0e7a1a54727beec0c449454a5706d5f64a5f9c7692a81110`, and ten
Leaf Forest Act 1 frames captured with `SAR_RENDERER=gl` were compared with
the software PPU frames: zero differing pixels and a maximum channel delta of
zero on every frame. The recorded GPU runs hold 59.6 to 59.7 frames per
second, and the per-frame cost of the snapshot, the upload, the submission
and the GPU is in [`docs/STAGE1.md`](STAGE1.md).

Not measured or not implemented: subpixel interpolation, high-refresh
schedule, lighting, audio latency, the performance targets at 1080p or 4K,
Sonic Advance 1, or a Windows/macOS host. The stage 1 layer path covers the
Leaf Forest slice only: affine backgrounds, colour effects, windows and the
object window are implemented but the recorded scene does not exercise them.

## References

- Upstream `SAT-R/sa2` at
  [`393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d`](https://github.com/SAT-R/sa2/commit/393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d):
  `src/main.c`, `src/core.c`, `src/platform/pret_sdl/sdl2.c`,
  `src/platform/shared/video/gpsp_renderer.cc`,
  `src/platform/shared/common.c`, `src/game/shared/stage/camera.c`,
  `src/game/sa2/stage/backgrounds/zone_1.inc.c`, `include/gba/defines.h`,
  `include/game/shared/stage/camera.h`.
- [`docs/CAPTURE.md`](CAPTURE.md): the M1 build, headless capture and windowed
  run record.
- [`tools/renderer_harness/README.md`](../tools/renderer_harness/README.md): the
  boundary harness and its fail-closed checks.
- [`tools/stage0/README.md`](../tools/stage0/README.md): the stage 0 build
  driver, host and verification helpers.
- [`tools/stage1/README.md`](../tools/stage1/README.md): the stage 1 layer
  path, the renderer switch, the capture and comparison helpers.
- [`docs/STAGE1.md`](STAGE1.md): the stage 1 record, its verification and its
  limits.
- [Issue #9](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/9):
  frame pacing, vsync and headroom baseline.
- [Issue #10](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/10):
  this architecture decision.
- [Issue #13](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/13):
  the stage 0 host and build driver.
- [GBATEK](https://problemkaputt.de/gbatek.htm): GBA PPU register and blending
  behaviour used for the parity work.
