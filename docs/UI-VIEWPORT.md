# The 240x160 UI field

[Back to README](../README.md) · [Renderer](RENDERER.md) · [Sonic Advance 1 UI](SA1-UI.md) · [Frame capture](CAPTURE.md) · [Issue #15](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/15)

**Status:** implemented as a project-owned module. The Sonic Advance 1 title
and zone select are composed on the 240x160 field they were authored on, and
the 426x240 gameplay world is left alone. The upstream port is unchanged: the
field is composed by the project's own module, not by a patch to the engine.

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
| the gameplay world | 426x240 | the stages |
| the UI field | 240x160 | the Sonic Advance 1 menus |

The space is a property of the scene, and the scene is set at the
screen/menu boundary. It is never inferred from the contents of a frame, from
an environment variable, from the presence of a background layer or from a
frame number. The module keeps the scene state
(`sar_ui_viewport_set_scene`); the seam that reads it from the engine is
`remaster/renderer/ui_viewport_port.c`.

The scene is the engine's own statement. The game creates a stage task when
a stage starts and destroys it when the stage ends, so the presence of that
task is what says whether the 426x240 world is on screen. Sonic Advance 2
always reports the gameplay world, because its screens are authored for it;
only Sonic Advance 1 has a 240x160 field.

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

The stage 0 host with the software PPU and with the stage 1 GPU layer path
still produces the recorded frame 1200 of the tape:

```text
134e4df9b462e85f0e7a1a54727beec0c449454a5706d5f64a5f9c7692a81110  frame_001200.ppm
```

and the stage 1 layer path still matches the software PPU on every pixel of
the reference frames of `tools/stage1/`. Sonic Advance 2 always reports the
gameplay world, so the field is not composed for it.

## What is not established

* The stage 0 driver still builds Sonic Advance 2 only. The Sonic Advance 1
  evidence uses the upstream capture path with a call of the module, not a
  supported driver target. Issue #22 adds the driver target.
* The stage 1 GPU layer path does not compose the 240x160 field yet. The
  field is composed by the project's own module into the software frame, so
  the two paths are alternatives, not a single renderer. Folding the field
  into the stage 1 layer path is stage 2 work.
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
  `src/platform/shared/video/gpsp_renderer.cc`.
- [docs/SA1-UI.md](SA1-UI.md): the traced cause.
- [docs/OAM.md](OAM.md): the canonical OAM table this module reads.
- [docs/RENDERER.md](RENDERER.md): the renderer architecture.
- [Issue #15](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/15).
