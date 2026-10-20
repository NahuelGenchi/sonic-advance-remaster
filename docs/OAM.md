# Canonical OAM

[Back to README](../README.md) · [Renderer](RENDERER.md) · [Sonic Advance 1 UI](SA1-UI.md) · [Frame capture](CAPTURE.md) · [Issue #14](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/14)

**Status:** implemented for the M2 stage 0 host. The canonical entry, the two
adapters and the writer-style detection live in
[`remaster/renderer/oam.h`](../remaster/renderer/oam.h); the host-side
boundary lives in [`remaster/host/oam_bridge.h`](../remaster/host/oam_bridge.h).
The stage 0 host converts the GBA-native records of the Sonic Advance 1
stage HUD into the extended layout before the software PPU draws, and the
Sonic Advance 1 HUD is at its authored positions in a recorded 426x240 run.
The upstream port is unchanged: no upstream file, diff or asset is in this
repository, and the OAM ABI of the game is the upstream one.

## The two views

The upstream decompilation has two mutually incompatible views of the same
128-entry OAM table, and the 426x240 build writes both of them:

| View | Record | Position | Attribute words | Writers |
| :--- | :--- | :--- | :--- | :--- |
| GBA-native `.all` | 8 bytes | Y in `attr0` bits 0-7, X in `attr1` bits 0-8 | `attr0`, `attr1`, `attr2` at offsets 0, 2, 4 | Sonic Advance 1 stage HUD and UI text (`src/game/sa1/stage/ui_rendering.c`) |
| Extended `.split` | 12 bytes | separate `s16` X and Y at offsets 0 and 2 | one 32-bit bitfield at offset 4, `attr2` at offset 8 | Sonic Advance 2 and the widescreen sprite path (`src/sprite.c`) |

The extended view repacks the attribute words: attribute 0's high byte takes
word bits 0-7, attribute 1's bits 9-15 take word bits 8-14, and the
remaining 17 bits are padding that no writer sets. The two views overlap in
the same bytes and cannot both be right, which is why the stage HUD was
invisible in the 426x240 build: the software PPU reads the extended view
only, and the HUD's GBA words landed in the extended view's coordinate
fields. [docs/SA1-UI.md](SA1-UI.md) traced this; the `EXTENDED_OAM FALSE`
build there is the diagnostic, not the design, because it changes the OAM
ABI of every sprite and drops the separate 16-bit coordinates.

## The canonical entry

```c
typedef struct {
    int16_t x;            /* signed screen position */
    int16_t y;            /* signed screen position */
    uint16_t attr0;       /* GBA attribute 0, bits 0-7 cleared */
    uint16_t attr1;       /* GBA attribute 1, bits 0-8 cleared */
    uint16_t attr2;       /* GBA attribute 2, complete */
    uint16_t affine_param;/* the entry's affine parameter word */
} SarOamEntry;
```

`x` and `y` are authoritative and the coordinate bits of the attribute
words are cleared, because the extended view carries positions that the GBA
9-bit X and 8-bit Y cannot hold. Every other attribute bit is kept: the
affine mode, the object mode, mosaic, the colour mode, the shape, the
matrix number, the flips, the size, the tile number, the priority, the
palette and the affine parameter.

The module offers both directions for both views:

| Function | Direction |
| :--- | :--- |
| `sar_oam_decode_gba_all`, `sar_oam_decode_extended_all`, `sar_oam_decode_extended_split` | raw record to canonical entry |
| `sar_oam_encode_gba_all`, `sar_oam_encode_extended_all`, `sar_oam_encode_extended_split` | canonical entry to raw record |
| `sar_oam_set_gba_all`, `sar_oam_set_extended_split`, `sar_oam_set_attrs` | one canonical entry from either writer style |
| `sar_oam_table_decode`, `sar_oam_rewrite_extended` | the 128-entry table |

## Coordinate rules

The GBA view addresses a 9-bit X and an 8-bit Y. The upstream software PPU
reads them as

```c
if (obj_x > DISPLAY_WIDTH)  obj_x -= 512;
if (obj_y > DISPLAY_HEIGHT) obj_y -= 256;
```

and the canonical module applies the same rule with the caller's screen
size, so a converted entry lands on the pixel the upstream native build
would draw. With the 426x240 field the GBA view reaches X in [-85, 426] and
Y in [-15, 240]. The extended view has no such limit: it carries signed
16-bit coordinates, and negative and off-screen positions are kept as they
are.

## Writer-style detection

A 12-byte record carries no tag for the view it was written with, but the
two views are distinguishable:

1. The extended `.split` view keeps 17 padding bits above its 15 attribute
   bits and no writer ever sets them, while the `.all` view stores the GBA
   attribute 1 word in exactly that half of the 32-bit attribute word. A
   non-zero upper half therefore proves that the record was written through
   the `.all` view. This is the exact test, and it is the only one the
   conversion uses, so a record that the extended view cannot draw never
   becomes a visible one.
2. The engine's unused entries are filled with the halfword pattern
   `0x0200` and belong to the layout the engine writes. They are read as
   extended records.
3. A `.all` record whose attribute 1 word is zero is the only ambiguous
   case. The classification of such a record falls back to the prohibited
   shape and object mode of the hardware, which only concerns records that
   are not drawn at all.

The remaining case cannot be resolved from the record alone: a `.all`
record at X = 0 with an 8x8 shape and no other attribute is read as an
extended record. A writer that needs the other reading can say so through
the `views` argument of `sar_oam_table_decode` and
`sar_oam_rewrite_extended`, which take the writer style of every entry
explicitly.

## The host boundary

[`remaster/host/oam_bridge.c`](../remaster/host/oam_bridge.c) runs once per
VBlank, between the VBlank interrupt and the software PPU draw:

1. it decodes the engine's OAM table into the canonical entries, which is
   what the renderer consumes;
2. it rewrites the GBA `.all` records of the engine's OAM buffer into the
   extended projection, in place, so the software PPU and the later GPU
   path read one layout.

The rewrite keeps the 128 entries, their order, the affine parameter words
and every record that is not a GBA `.all` record byte for byte as the
engine wrote it, and it skips hidden records, which are not drawn in either
view. The engine's staging buffer is not touched, and the engine overwrites
the OAM buffer on its next VBlank, so the conversion is a presentation
step and not a change of game state.

`SAR_OAM_BRIDGE=0` disables the bridge, which is how the before and after
frames of the Sonic Advance 1 run below were recorded from the same
binary.

## Verification

### Module tests

```sh
tools/oam_test.sh
```

The test compiles the module and its test program with
`-Wall -Wextra -Werror` and checks 632 assertions over synthetic entries:
a normal sprite, an affine sprite, a hidden sprite, horizontal and vertical
flips, mosaic, the 256-colour bit, priority, palette, tile number, shape
and size, negative and wrapped coordinates, the round trip to both views,
the writer-style detection, the rewrite and the 128-entry table. It also
proves that the GBA-native `.all` writer and the extended `.split` writer
produce the same canonical entry for the same sprite.

### Sonic Advance 2 regression

```sh
SA2_UPSTREAM=/tmp/opencode/upstream/sa2 \
SA2_ROM="$SA2_ROM" \
SAR_BUILD_ROOT=/home/rayzza/sar-builds/agent-01/sa2-stage0 \
tools/stage0/build.sh

cd /home/rayzza/sar-builds/agent-01/sa2-stage0
SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy SAR_VSYNC=0 \
  SAR_CAPTURE_DIR=./capture SAR_CAPTURE_FRAME=1200 \
  SAR_EXIT_AFTER_FRAME=1200 SAR_SAVE_PATH=./sa2.sav \
  ./sa2-stage0
```

The software frame and the GL back-buffer readback of frame 1200 both hash
to `134e4df9b462e85f0e7a1a54727beec0c449454a5706d5f64a5f9c7692a81110`,
the same value as the recorded stage 0 and M1 captures. Sonic Advance 2
writes the extended view only, so the bridge is a no-op for it: the OAM
buffer is byte for byte the engine's.

### Sonic Advance 1 stage HUD

The stage 0 driver builds Sonic Advance 2 only, so the Sonic Advance 1 run
uses the same upstream snapshot with the game's own build settings. The
commands are in the delivery record of issue #14; the core archive is the
upstream `sa1` build without `src/platform/pret_sdl/sdl2.o`, and the host
is compiled with `-DGAME=GAME_SA1 -DEUROPE -DENABLE_DECOMP_CREDITS=0`.

```sh
cd /home/rayzza/sar-builds/agent-01/sa1-stage0
SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy SAR_VSYNC=0 SAR_SCRIPT=sa1 \
  SAR_CAPTURE_DIR=./cap-bridge SAR_CAPTURE_FRAME=1400 \
  SAR_EXIT_AFTER_FRAME=1400 SAR_SAVE_PATH=./sa1-bridge.sav \
  ./sa1-stage0
```

`SAR_SCRIPT=sa1` replays the input schedule of the M1 capture path, so the
run reaches the same stage at the same VBlank as the recorded
[`sa1-ui-hud-before.png`](images/sa1-ui-hud-before.png) and
[`sa1-ui-hud-after.png`](images/sa1-ui-hud-after.png) frames.

| Frame | SHA-256 | Comparison |
| :--- | :--- | :--- |
| bridge off, software frame | `c015c7a7c10b3a48005e21ee6d2e4208dd73d95bda97b33f2cf3449dc615a552` | 0 differing pixels against `sa1-ui-hud-before.png` |
| bridge on, software frame | `b9bb44bf0d12d66c48bfd515c810bdc43252e12806f8ed9944d26c4b57b6bd57` | 0 differing pixels against `sa1-ui-hud-after.png` |
| bridge on, GL readback | `b9bb44bf0d12d66c48bfd515c810bdc43252e12806f8ed9944d26c4b57b6bd57` | same as the software frame |

The bridge changes 3936 of the 102240 pixels of the frame, all of them in
the HUD: the ring icon, the ring count and the time at the top left, and
the lives icon at the bottom left. The frame with the bridge on is
identical to the frame of the `EXTENDED_OAM FALSE` diagnostic, without the
compile-time switch and without changing the OAM ABI.

The committed frame
[`sa1-hud-oam-bridge.png`](images/sa1-hud-oam-bridge.png) is that recorded
frame, converted with `tools/ppm_to_png.py --scale 2` like the other
committed images.

The recorded OAM table of that frame holds twelve GBA `.all` records (the
HUD) and two extended records (the stage sprites); the remaining 114
entries are the engine's unused-entry pattern and are left untouched.

## What is not established

- The stage 0 driver still builds Sonic Advance 2 only. The Sonic Advance 1
  run above is a manual build of the same host against the `sa1` core
  archive, not a supported driver target.
- The upstream port is unchanged. The bridge lives in the host, so the
  upstream Sonic Advance 1 port still shows the corrupted HUD. A port-side
  writer shim is the follow-up described in the issue.
- The M2 renderer does not exist yet. The canonical table is built and
  exposed by the bridge, but nothing consumes it yet; the stage 0 host
  still presents the upstream software PPU frame.
- The one ambiguous `.all` record of the writer-style detection is not
  reachable in the recorded frames, but it is a real limit of the rule.

## References

- Upstream `SAT-R/sa2` at
  `393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d`: `include/gba/types.h`,
  `include/gba/defines.h`, `src/sprite.c`, `src/core.c`,
  `src/game/sa1/stage/ui_rendering.c`,
  `src/platform/shared/video/gpsp_renderer.cc`.
- [docs/SA1-UI.md](SA1-UI.md): the traced cause.
- [docs/RENDERER.md](RENDERER.md): the renderer architecture, invariant 4.
- [tools/stage0/README.md](../tools/stage0/README.md): the stage 0 host.
- [Issue #14](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/14).
