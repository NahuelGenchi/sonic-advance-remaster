# The 240x160 UI field

[Back to README](../README.md) · [Renderer](RENDERER.md) · [Sonic Advance 1 UI](SA1-UI.md) · [Frame capture](CAPTURE.md) · [Issue #15](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/15) · [Issue #26](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/26)

**Status:** implemented as a project-owned module. The Sonic Advance 1 title
and zone select, and the Sonic Advance 2 language select and character select,
are composed on the 240x160 field they were authored on, and the 426x240
gameplay world is left alone. The upstream port is unchanged: the field is
composed by the project's own module, not by a patch to the engine.

## The problem

The Sonic Advance 1 menus are authored on the GBA's 240x160 field. The port
renders them into a 426x240 frame, so the text-background sampler read the
maps as if they were 426x240 wide:

* a 256-wide map wrapped at 256, so the left third of the art repeated;
* the rows 160-239 of the map were never written by the game, so they showed
  whatever VRAM happened to hold;
* the shared text-BG path also carried a Sonic Advance 2 specific hack that
  disabled the second 256-pixel block of a 512-wide map.

[docs/SA1-UI.md](SA1-UI.md) traced this from the screen code down to the
software PPU. The game never claimed its maps contained 426x240 of art.

## The model

A frame is composed in one of two coordinate spaces:

| Space | Size | Who is in it |
| :--- | :--- | :--- |
| the gameplay world | 426x240 | the stages, the stage intro, the Sonic Advance 2 title and multiplayer screens |
| the UI field | 240x160 | the Sonic Advance 1 menus, the Sonic Advance 2 language select, character select and options screens |

The space is a property of the scene, and the scene is set at the
screen/menu boundary. It is never inferred from the contents of a frame, from
an environment variable, from the presence of a background layer or from a
frame number. The module keeps the scene state
(`sar_ui_viewport_set_scene`); the seam that reads it from the engine is
`remaster/renderer/ui_viewport_port.c`.

The scene is the engine's own statement. The game creates a stage task when
a stage starts and destroys it when the stage ends, so the presence of that
task is what says whether the 426x240 world is on screen. For the screens
between stages the seam reads the display mode: the title screen and the
multiplayer screens set `DISPCNT_MODE_1` and lay their elements out against
`DISPLAY_WIDTH`, while the language select, character select and options
screens leave the display in mode 0 and author 30x20 and 32x32 maps on the
GBA field. Sonic Advance 1 keeps the stage-task rule alone; its menus were
already verified on the field and the display mode is not needed to separate
them from its stages.

## Composition, not cropping

The field is composed from the engine state: VRAM, the palette, the canonical
OAM table, the display and background control registers, the scroll registers
and the affine parameters. Nothing is cropped out of an already-rendered
426x240 frame, which is what the prototype in `docs/SA1-UI.md` did and what
made it leak into gameplay.

The composition runs the same scanline loop the upstream software PPU runs,
with the field's scanline in place of the software pixel loop. The engine's
H-blank handler runs inside that loop, so the per-scanline scroll registers
the engine writes are the ones the field is sampled with, and the affine
reference is stepped per line the way the hardware steps it.

Because the field is composed from the engine state, a screen that authors
30x20 tiles shows those 30x20 tiles and nothing else. The map rows and
columns the screen never wrote are outside the field and are never sampled.

## Map geometry

The sampler honours the BGCNT map size and the tile stride. The two layouts
the games use are both in the sampler, and the scene picks one:

| Layout | Shape | Used by |
| :--- | :--- | :--- |
| `SAR_UI_MAP_LAYOUT_GBA` | 32x32 tile blocks, 32 tiles per row inside a block, the second 256-pixel block of a 512-wide map in the next 2 KB block | the UI maps of both games |
| `SAR_UI_MAP_LAYOUT_WIDE` | one linear strip of 64 tiles per row | the widescreen level maps of the port |

The 512-wide handling is a mode here, not a global hack in a shared path, so
the Sonic Advance 2 widescreen maps keep their own addressing and the
Sonic Advance 1 UI maps address their second block correctly.

## Presentation

The field is presented with one uniform scale and letterboxed. The integer
scale is used when the frame allows it: at 426x240 the field is presented at
240x160, centred, with the rest of the frame black. The fractional fit is
the documented fallback for a frame smaller than the field. The field is
never stretched on one axis and never cropped.

A gameplay frame is not affected: the scene is the gameplay world, the
software PPU draws the 426x240 frame, and the field is not composed at all.
There is no cropped gameplay frame left behind when the scene changes,
because the gameplay world is never rendered into the field's frame.

## Verification

### Module tests

```sh
tools/ui_viewport_test.sh
```

compiles the module, the canonical OAM module and the test program with
`-Wall -Wextra -Werror` and prints:

```text
ui_viewport_test: all checks passed
```

The tests cover the map geometry of the four text map sizes, the GBA and the
widescreen layouts, the 32- and 64-tile strides, the map base block, the
sampling of the map rows and columns outside the authored extents, the
scene state, the uniform scaling and the letterboxing.

### Sonic Advance 1

The stage 0 driver builds Sonic Advance 2 only, so the Sonic Advance 1 run
uses the upstream snapshot with the capture hook of `docs/CAPTURE.md` and one
call of the module in the upstream capture path. The command and the tree are
in the delivery record of issue #15; the module itself is the project's, and
the upstream tree is a disposable copy outside this repository.

| Frame | SHA-256 | What it shows |
| :--- | :--- | :--- |
| title, VBlank 800 | `ff4c45e36807d758f4061d8aff210994ee561535597a0f26be19395be6af4c9b` | the authored title, centred in the 426x240 frame, no repeated columns and no garbage band |
| zone select, VBlank 1100 | `2da7912df49765c885dcb2aabd83cb75b2046d1a0a604da01b136a61f4a5a50f` | the authored zone list, same |
| gameplay, VBlank 1400 | `c015c7a7c10b3a48005e21ee6d2e4208dd73d95bda97b33f2cf3449dc615a552` | the unchanged 426x240 world |

The gameplay frame is the same file the Sonic Advance 1 HUD is captured
without the OAM bridge, so the gameplay world is byte for byte what it was
before this change.

The committed frames
[`sa1-ui-title-viewport.png`](images/sa1-ui-title-viewport.png) and
[`sa1-ui-zone-select-viewport.png`](images/sa1-ui-zone-select-viewport.png)
are those recorded frames, converted with `tools/ppm_to_png.py --scale 2`
like the other committed images.

### Sonic Advance 2

The Sonic Advance 2 launch screens are split by the engine's display mode:

* the language select, character select and options screens set
  `gDispCnt = 0x1740`, whose mode bits are 0. They author 30x20 and 32x32
  maps on the 240x160 field, so they are composed on the field;
* the title and the multiplayer screens set `DISPCNT_MODE_1` (the affine BG2
  mode) and lay their elements out against `DISPLAY_WIDTH`, so they stay on
  the 426x240 world. The stage task covers the stage intro and gameplay.

The title could not be moved to the field: its logo and PRESS START are
positioned for the 426x240 frame and its text backgrounds load
`DISPLAY_WIDTH/8` tile columns. Composing it on the 240x160 field lost the
logo and the right side of the screen. The cloud map is still 256 columns
wide, so the world frame shows the cloud layer repeating at x = 256; that is a
separate title defect, not a field defect.

The recorded run starts from a fresh save with the software PPU:

```sh
cd /home/rayzza/sar-builds/fix-26
SAR_RENDERER=software \
SAR_CAPTURE_DIR=/home/rayzza/sar-builds/fix26-mode \
SAR_CAPTURE_FRAME=30,150,180,270,450,480,490,510,750,1200 \
SAR_EXIT_AFTER_FRAME=1201 \
SAR_SAVE_PATH=/home/rayzza/sar-builds/fix26-mode/sa2.sav \
SAR_VSYNC=0 SDL_VIDEODRIVER=x11 ./sa2-stage0
```

| VBlank | Screen | Scene | Before `frame_*.ppm` | After `frame_*.ppm` |
| :--- | :--- | :--- | :--- | :--- |
| 30 | language select | field | `447ad6141c280a37fbf43ae2a676effe5bec73c29197010b5fa80a71f54dcba0` | `b88632ec7c8349ca7844406c6343a355696fbb00c91b925b38a2b87db05bc6f1` |
| 150 | title intro | world | `f0e6e416924003c36e8874ef1375236e749a926a90ce86f3c28c16f86ef1f0eb` | `f0e6e416924003c36e8874ef1375236e749a926a90ce86f3c28c16f86ef1f0eb` |
| 180 | title | world | `c23be93f87fca495d0c600cee8b6a7f0c9464d1a7e96b3db6029d174a93be8b5` | `c23be93f87fca495d0c600cee8b6a7f0c9464d1a7e96b3db6029d174a93be8b5` |
| 270 | title | world | `c23be93f87fca495d0c600cee8b6a7f0c9464d1a7e96b3db6029d174a93be8b5` | `c23be93f87fca495d0c600cee8b6a7f0c9464d1a7e96b3db6029d174a93be8b5` |
| 450 | character select | field | `79eb392a47464591553eb49caac08b91a7f0da688d124e2d8dcf2cf16b9e104e` | `1e68e3f677dbf66b22b84d86623dbbb23e6e8031dcd9b102fa5e5688cf22435f` |
| 480 | character select, Sonic chosen | field | `65bd27151149f1243ecfebfe32410a2272b009846af232ee0fb075c76e9fc296` | `9d5e1c3053fe4cbc17b62fd27c40d60a9503dee658d492a89edef3a2cc8803ad` |
| 510 | Leaf Forest Act 1 | world | `4768af9337d9e9a31865b8ff4e9b51da939176f984ae24a83c6a7f164d733d80` | `4768af9337d9e9a31865b8ff4e9b51da939176f984ae24a83c6a7f164d733d80` |
| 750 | Leaf Forest Act 1 | world | `5bd2f2c656fa0f06bdf3c3ebfb90bcb126e4be2ebfd2e3149f83761102a095a6` | `5bd2f2c656fa0f06bdf3c3ebfb90bcb126e4be2ebfd2e3149f83761102a095a6` |
| 1200 | Leaf Forest Act 1 | world | `134e4df9b462e85f0e7a1a54727beec0c449454a5706d5f64a5f9c7692a81110` (issue #15 reference) | `134e4df9b462e85f0e7a1a54727beec0c449454a5706d5f64a5f9c7692a81110` |

The language select and character select frames change because the duplicate
columns and the unwritten rows are gone: the field is composed at its authored
240x160 and presented centred in the 426x240 frame, and the rows and columns
outside its maps are never sampled. The character-select banner is an OAM
sprite whose widescreen exit runs past the field's right edge; the field clips
it at x = 240, as the native 240 build does, and does not move it back into
the frame. The title, stage and gameplay frames are byte for byte the frames
of the before run. The stage intro is created inside
the stage task (`src/game/shared/stage/stage.c:384`), so the stage-task branch
returns the world for it; the separate before run in
`/home/rayzza/sar-builds/launch-intro` records those intro frames in the
426x240 world, and this change does not touch that branch. The fresh-save run
cuts from the field at frame 508 to the world at frame 510. The final gameplay
frame matches the reference recorded for issue #15.

The committed frames [`sa2-launch-language-before.png`](images/sa2-launch-language-before.png)
and [`sa2-launch-character-select-before.png`](images/sa2-launch-character-select-before.png)
are the recorded before frames 30 and 450; the matching
[`sa2-launch-language-field.png`](images/sa2-launch-language-field.png),
[`sa2-launch-character-select-field.png`](images/sa2-launch-character-select-field.png)
and [`sa2-launch-title-world.png`](images/sa2-launch-title-world.png) are the
after frames 30, 450 and 180. All are converted with
`tools/ppm_to_png.py --scale 2`.

The stage 1 GPU layer path still matches the software PPU on every pixel of
the ten reference frames of `tools/stage1/` (frames 800, 950, 1100, 1200,
1300, 1450, 1600, 1800, 2000 and 2150; 0 differing pixels, 10 of 10).

## What is not established

* The stage 0 driver still builds Sonic Advance 2 only. The Sonic Advance 1
  evidence uses the upstream capture path with a call of the module, not a
  supported driver target. Issue #22 adds the driver target.
* The Sonic Advance 2 title stays on the 426x240 world. Its 256-wide cloud map
  repeats at x = 256, and the title intro briefly draws four copies of the
  logo at VBlank 150 (the before and after frames are byte for byte the same,
  so this change did not introduce or remove either). Both are separate title
  defects; the field cannot reproduce the title because its logo and PRESS
  START are positioned against `DISPLAY_WIDTH` and its text backgrounds load
  `DISPLAY_WIDTH/8` columns.
* The Sonic Advance 2 multiplayer screens also use `DISPCNT_MODE_1` and are
  classified as the world; they are not captured by the launch tape, so the
  classification is not verified on a recorded frame.
* The field composer draws text backgrounds, the canonical OAM sprites and
  the background sprites it supports. It does not implement windows, alpha
  blending, mosaic or affine OAM sprites, and the captured launch screens do
  not need them. A screen that combines the field with those effects would
  need the stage 1 path to compose the field.
* The stage 1 GPU layer path does not compose the 240x160 field yet. The
  field is composed by the project's own module into the software frame, so
  the two paths are alternatives, not a single renderer. A
  `SAR_RENDERER=gl` run still shows the launch screens in the world. Folding
  the field into the stage 1 layer path is stage 2 work.
* The Sonic Advance 1 field is presented at 240x160 inside the 426x240
  frame. The scale policy at the presentation resolution, and whether the
  field is ever expanded beyond its authored size, are M2 decisions that the
  stage 1 presentation path has not settled.
* Staff credits and the other Sonic Advance 1 screens that use a background
  layer are not captured here. The character select needs the BG-sprite
  composition of issue #16 as well.

## References

- Upstream `SAT-R/sa2` at
  `393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d`: `include/gba/types.h`,
  `include/gba/defines.h`, `src/game/sa1/menus/title_screen.c`,
  `src/game/sa1/ui/course_select.c`,
  `src/game/sa2/title_screen.c`, `src/game/sa2/ui/character_select.c`,
  `src/game/sa2/options_screen.c`, `src/game/shared/stage/stage.c`,
  `src/platform/shared/video/gpsp_renderer.cc`.
- [docs/SA1-UI.md](SA1-UI.md): the traced cause.
- [docs/OAM.md](OAM.md): the canonical OAM table this module reads.
- [docs/RENDERER.md](RENDERER.md): the renderer architecture.
- [Issue #15](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/15).
- [Issue #26](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/26).
