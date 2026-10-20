# Stage 1 layer path

This is the Stage 1 GPU layer path of the M2 renderer: the project-owned code
that draws the GBA backgrounds and objects with OpenGL 3.3 core from a
per-tick snapshot of the game's state.

It is **not** the complete remaster renderer. There is no subpixel camera, no
high-refresh schedule, no lighting and no upscaled assets here; those are
stages 2 to 4 of the slice in [`docs/RENDERER.md`](../../docs/RENDERER.md).

## What it does

| Piece | Where |
| :--- | :--- |
| Snapshot interface and the scanline capture | [`remaster/renderer/snapshot.h`](../../remaster/renderer/snapshot.h), [`snapshot.c`](../../remaster/renderer/snapshot.c) |
| GL 3.3 core entry points | [`remaster/renderer/gl_api.h`](../../remaster/renderer/gl_api.h), [`gl_api.c`](../../remaster/renderer/gl_api.c) |
| Layer rasterization, objects and the composite pass | [`remaster/renderer/layer_path.c`](../../remaster/renderer/layer_path.c) |
| GLSL 330 shaders | [`remaster/shaders/stage1_layers.h`](../../remaster/shaders/stage1_layers.h) |
| Host integration and the renderer switch | [`remaster/host/main.c`](../../remaster/host/main.c), [`present.c`](../../remaster/host/present.c) |
| Frame comparison tool | [`compare_ppm.py`](compare_ppm.py) |
| Capture helper for the reference frames | [`capture_slice.py`](capture_slice.py) |

## The renderer switch

The host owns the renderer choice through one environment variable:

| Value | Behaviour |
| :--- | :--- |
| `SAR_RENDERER=software` | The upstream software PPU draws the frame and the host uploads it. This is the default and the reference. |
| `SAR_RENDERER=gl` | The Stage 1 layer path draws the frame. The software PPU is not called at all. |

Anything else is a start-up error (exit status 2).

Failure behaviour:

* A missing GL 3.3 core context, a missing entry point, a shader that does
  not compile or an incomplete framebuffer **fails closed**: the host prints
  the reason and exits with status 1 before a frame is presented. A black
  window is never reported as success.
* `SAR_ALLOW_SOFTWARE_FALLBACK=1` makes a start-up failure of the GPU path
  fall back to the software PPU with a clear message on standard error. The
  fallback is only for start-up; a frame that fails to render later still
  fails closed.
* A GL error during a frame is reported and the frame is not presented.

## The snapshot

The snapshot is the only thing the GPU path reads. It is captured once per
game tick at the point where the upstream software PPU would start drawing:
after the VBlank interrupt ran and the VBlank DMA copied the display
registers, before the game prepares the next frame.

`remaster/renderer/snapshot.c` runs the same scanline state machine as
`gpsp_draw_frame` in the upstream software PPU:

1. set `VCOUNT` and dispatch the VCount interrupt;
2. reload the affine reference counters when the game changed them;
3. record the display state of the line;
4. run the HBlank DMA and the HBlank interrupt;
5. step the affine reference counters for the next line.

Because the state is recorded per line, a register that the game changes
during HBlank is reproduced exactly. That is what makes the Leaf Forest BG3
per-scanline scroll effect work: the game copies four bytes into `BG3HOFS`
and `BG3VOFS` on every HBlank, and the layer path sees the same value on the
same line as the software PPU.

Per line the snapshot records:

| Data | Notes |
| :--- | :--- |
| `HOFS`, `VOFS`, `BGxCNT` for BG0 to BG3 | Text and affine layers |
| `PA`, `PB`, `PC`, `PD` for BG2 and BG3 | The text layers have no affine registers |
| BG2 and BG3 reference X and Y | 8.8 fixed point, stepped like the PPU steps them |
| `DISPCNT`, `BLDCNT`, `BLDALPHA`, `BLDY`, `MOSAIC` | Display and colour effect control |
| `WIN0H`, `WIN0V`, `WIN1H`, `WIN1V`, `WININ`, `WINOUT` | Window control |

Per frame the snapshot records the OAM (128 entries of 12 bytes), the
palettes (512 entries) and a pointer to the VRAM.

### VRAM dirty ranges

The upstream game does not expose a VRAM write hook, so the layer path finds
the dirty range by hashing: VRAM is split into 1 KB pages, each page is
hashed every tick and only the pages whose hash changed are uploaded to the
GPU. A 120 KB copy per tick is avoided; the cost is one hash pass over the
VRAM, which is measured in the performance log.

## What the layer path implements

* **Text backgrounds**: 256x256, 512x256, 256x512 and 512x512 maps, 16 and
  256 colour tiles, horizontal and vertical flips, per-scanline scroll, and
  the mosaic of the background.
* **Affine backgrounds**: `PA` to `PD`, the reference registers, the wrap
  bit, 256 colour tiles and the vertical mosaic of the reference stepping.
* **Objects**: shape and size, tile, palette, flips, affine objects with
  double size, priority, mosaic and the object mode (normal, semi
  transparent, object window).
* **The composite pass**: the layer priority order of the software PPU, the
  colour effect targets, alpha blending, brightness and darkness, the windows
  and the object window. The blend decisions are made on the integer 5-bit
  colour exactly like the software PPU, and the result is converted to linear
  float for the scene target.

### The tile walk

The upstream software PPU does not compute a map address per pixel: it walks
the map of a scanline tile by tile, restarts at the first map entry when the
map width is used up, and reads the row in groups of 32 tiles. The last
partial tile of a line is read from the position where the walk stopped,
which can be past the end of the map row.

The layer path reproduces that walk in `PpuTextTile` in the background shader.
Without it the right edge of a 256 pixel wide background and the tail of
every line differ from the software PPU.

## Coverage and known limits

Implemented and compared against the software PPU:

* text backgrounds with the software PPU tile walk, including the widescreen
  screen bases (the 426 pixel build uses a 6 bit screen base);
* the per-scanline HBlank scroll of Leaf Forest;
* objects, including their priority order.

Compared against the software PPU on the recorded tape (Leaf Forest Act 1,
the act transition, Leaf Forest Act 2 and the boss):

* text backgrounds with the software PPU tile walk, including the widescreen
  screen bases (the 426 pixel build uses a 6 bit screen base);
* the per-scanline HBlank scroll of Leaf Forest;
* objects, including their priority order;
* the colour effects and the windows, which the boss scene uses.

Implemented but **not exercised by the recorded tape** (no reference frame
exists for them in this repository):

* affine backgrounds (the recorded scenes run in video mode 0, where every
  background is a text background);
* the object window;
* object mosaic.

Known limits, in order of importance:

* The composite pass keeps the frontmost object and the frontmost background
  per pixel. The software PPU keeps the two frontmost pixels of the whole
  stack, so a semi-transparent object over another object is blended against
  the background instead of the object below it, and a transparent background
  layer shows the backdrop with the backdrop flags where the PPU shows it
  with the flags of that layer. The recorded scenes do not use semi
  transparent objects.
* The mosaic phase of a background is anchored at the screen origin. The
  software PPU anchors it at the start of the window segment, so the two
  differ inside a window.
* The object window mask is rendered but the object window is not used by
  Leaf Forest.

## Recording the reference frames

The capture helper runs the host twice on the same tape and compares the
frames:

```sh
python3 tools/stage1/capture_slice.py \
    --binary /home/rayzza/sar-builds/agent-04/sa2-stage0 \
    --frames 800,950,1100,1200,1300,1450,1600,1800,2000,2150 \
    --out /tmp/slice
```

It prints, for every frame, the number of differing pixels and the maximum
channel delta against the software PPU frame, and it exits non-zero when a
frame is over the tolerance.

The same comparison can be run on two captured files:

```sh
python3 tools/stage1/compare_ppm.py reference.ppm candidate.ppm --tolerance 0
python3 tools/stage1/compare_ppm.py reference.ppm candidate.ppm --diff diff.ppm
```

## Debug aids

The layer path writes review aids when the matching environment variable is
set. They are not part of the game path.

| Variable | Effect |
| :--- | :--- |
| `SAR_PERF_LOG=<path>` | CSV with the capture, upload, render and GPU time per frame |
| `SAR_SNAPSHOT_DUMP=<path>` | The per-line register state of the dumped frame |
| `SAR_VRAM_DUMP=<path>` | The VRAM of the dumped frame |
| `SAR_OAM_DUMP=<path>` | The OAM of the dumped frame |
| `SAR_PALETTE_DUMP=<path>` | The palettes of the dumped frame |
| `SAR_LAYER_DUMP=<prefix>` | The layer targets of the dumped frame, as PPM files |
| `SAR_DUMP_FRAME=<n>` | The frame the dumps are taken from (default: every frame) |
