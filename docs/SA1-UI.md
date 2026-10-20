# Sonic Advance 1 UI at 426x240

[Back to README](../README.md) · [Renderer](RENDERER.md) · [Frame capture](CAPTURE.md) · [Issue #11](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/11) · [Upstream #274](https://github.com/SAT-R/sa2/issues/274)

**Status:** research for M2. No fix is shipped. Every "after" frame below
comes from a local, uncommitted experiment in a private upstream checkout at
`393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d`; the repository contains the
documentation and the frames, not the patch. The upstream tree, ROM and saves
stay outside the repository.

This document separates three different failures that the upstream SDL port
shows together at 426x240:

| Scene | What the 426x240 port shows | Cause | Tested locally | Shipping status |
| :--- | :--- | :--- | :--- | :--- |
| Title, main menu, zone select | Art repeats at x >= 256 and the bottom 80 rows are garbage | 240x160 UI maps are sampled by a 426x240 viewport; the shared text-BG path also carries a SA2-specific 512-wide hack | 240x160 UI viewport | Not shipped; follow-up #15 |
| Stage HUD | Rings, time and the character icon are missing | SA1 HUD writers use the 8-byte GBA `.all.attr*` view; the 426x240 build uses the 12-byte `.split` view | `EXTENDED_OAM FALSE` diagnostic | Canonical OAM bridge in the stage 0 host, [docs/OAM.md](OAM.md); the upstream port is unchanged |
| Character select | Backdrop is stale, colourful garbage at 240x160 and 426x240 | The BG-sprite compositor is stubbed under `PORTABLE` and the text-BG map fill is disabled, so the backdrop map is never written | Fill the BG1 map with the animation's base tile | Not shipped; follow-up #16 |

The first two are presentation faults in the port; the third is a port/
decompilation gap that is independent of widescreen. The game-side art and
writes are correct for the GBA's 240x160 field in all three cases.

## What was traced

### From the screen code to the renderer

1. `CreateTitleScreen` (`src/game/sa1/menus/title_screen.c:341`) sets
   `gDispCnt = 0x41` and the BG control registers at `:349-351`
   (`gBgCntRegs[2] = 0x568C`, `gBgCntRegs[0] = 0x1480`). The BG0 title art is
   loaded by `DrawBackground` with `targetTilesX = 30`, `targetTilesY = 20` and
   `layoutVram = BG_SCREEN_ADDR(20)` (`title_screen.c:413-425`): 30x20 tiles is
   exactly 240x160.
2. `CreateCourseSelect` (`src/game/sa1/ui/course_select.c:148`) sets
   `gBgCntRegs[0] = 0x1D83` and `gBgCntRegs[1] = 0x1886` (`:211-213`). The list
   layer is loaded through `sub_80528AC` (`:111-132`) with a `0x500`-byte
   layout (`course_select.c:124`), which is 640 16-bit entries: 20 rows of 32
   tiles. Rows 20-31 are never written.
3. `CreateCharacterSelectionScreen`
   (`src/game/sa1/ui/character_select.c:116`) sets `gDispCnt = 0x1240` and
   `gBgCntRegs[1] = 0x1907` (`:145-146`). A BG sprite is created for animation
   723 and registered with `DisplaySprite_BG` (`:234-253`); it is re-registered
   every frame by `Task_805B984` (`:1561-1578`).
4. The stage HUD is drawn through `sub_80530CC` / `sub_8052F78`, called from
   `sub_8053BAC` (`src/game/sa1/stage/stage_ui.c:304-342`); both write
   `OamData.all.attr0/1/2` in
   `src/game/sa1/stage/ui_rendering.c:317-380` and `:383-446`.
5. The SDL platform draws the windowed frame through `VDraw`
   (`src/platform/pret_sdl/sdl2.c:673-678`); the per-frame entry point is
   `VBlankIntrWait` (`:296-400`), whose headless branch returns before drawing,
   so the capture hook adds the `gpsp_draw_frame` call and the PPM writer there
   (`docs/CAPTURE.md`). The frame itself is drawn by `gpsp_draw_frame`
   (`src/platform/shared/video/gpsp_renderer.cc:2286`).
6. Text backgrounds are rasterised by `render_scanline_text_fast` /
   `render_scanline_text_mosaic`
   (`gpsp_renderer.cc:341-482`, `:486-605`). Objects are prepared and drawn by
   `render_scanline_objs` (`gpsp_renderer.cc:1433-1516`) and the object scan
   `order_obj` (`gpsp_renderer.cc:1524-1619`).
7. BG sprites go through `DisplaySprite_BG` (`src/background.c:821`), the
   per-frame `sub_8004010` fill (`src/background.c:1058`) and the `sub_80039E4`
   compositor, both called from `sVblankFuncs` (`src/core.c:228-236`).

### Display and storage constants

At the pin, `include/gba/defines.h:52-53` sets `DISPLAY_WIDTH 426` and
`DISPLAY_HEIGHT 240`. That makes `WIDESCREEN_HACK TRUE` and
`EXTENDED_OAM TRUE` (`defines.h:58-63`), so OAM entries are 12 bytes
(`defines.h:69-74`). The GBA-native build uses 8-byte entries.

The UI art itself is still GBA-sized. The title BG0 and the zone-select BG1 are
256x256 text maps; the title BG2 is a 512x256 map. Only the GBA-sized regions
are written: 30x20 tiles for the title BG0, 24x10 for the title BG2 logo and
20x32 for the zone-select list. Nothing in the port re-authors them for 426x240.

## Root cause 1: a 240x160 UI field sampled by a 426x240 viewport

The title, menu and zone-select maps are correct for the GBA. The renderer
samples them as if the field were 426x240:

- For a 256-wide text map, `render_scanline_text_fast` reduces the horizontal
  offset modulo 256 (`gpsp_renderer.cc:385`, and `:522` in the mosaic variant).
  At 426 pixels wide, columns 256-425 wrap to 0-169, so the left part of the
  art repeats.
- For a 512-wide map, the same function contains a SA2-specific hack: the code
  that would address the second 256-pixel block is commented out under
  `// HACK: for SA2 we need this logic disabled to enable >240 width`
  (`gpsp_renderer.cc:373-383`; the mosaic variant keeps the same commented-out
  addressing without the comment, `:512-522`). A 512x256 SA1 UI map is
  therefore not addressed correctly either.
- The renderer walks all 240 scanlines. The UI maps only have 20 authored
  rows. Rows 160-239 read map entries that were never written, which is the
  garbage band at the bottom of the 426x240 frames.

The 426x240 before frames are [title](images/sa1-ui-title-before.png) and
[zone select](images/sa1-ui-zone-select-before.png). The native 240x160 build
shows the title screen without either defect
([title](images/sa1-ui-title-native-240x160.png)). That is the control: the art,
palettes and tile data are intact; the 426x240 viewport is what exposes the
unowned map area.

This is a renderer fault, not a game-logic fault. The game never claimed that
its maps contained 426x240 of art.

## Root cause 2: two incompatible OAM layouts

`EXTENDED_OAM` is not just a different entry size. The two `OamData` unions in
`include/gba/types.h` have different field orders:

- GBA-native (8 bytes, `types.h:101-154`, `OAM_DATA_SIZE_AFFINE` at `:180`):
  `y` and `x` are bitfields inside `attr0`/`attr1`; `.split` and `.all` overlay
  the same bytes.
- Extended (12 bytes, `types.h:186-241`, `OAM_DATA_SIZE_AFFINE` at `:258`):
  `split.x` and `split.y` are separate `s16` values before the attribute words;
  `.all` is a separate struct with `x`, `y`, `attr0`, `attr1`, `attr2`,
  `affineParam`.

The 426x240 build selects the extended layout. SA2 and the extended sprite path
write `split.x` / `split.y`. The SA1 stage HUD code was ported from GBA and
still writes `.all.attr0/1/2`. The renderer reads `split.x` / `split.y`
(`gpsp_renderer.cc:1451-1452`, `:1559-1572`) and strides entries by
`OAM_DATA_SIZE_AFFINE` (`:1445`, `:1538`), which is 12 in this build. The two
sides therefore disagree about both the entry stride and the field positions.
The HUD entries are not interpreted as the HUD.

Compiling with `EXTENDED_OAM FALSE` makes both sides use the 8-byte GBA layout.
The HUD then appears at the authored positions
([after](images/sa1-ui-hud-after.png)). That is a diagnostic, not a shipping
fix: it reduces the OAM entry size for every sprite and removes the separate
16-bit coordinates that the widescreen path uses. The shipping fix is one
canonical internal layout with an explicit adapter for both writer styles
(follow-up #14).

That adapter is [docs/OAM.md](OAM.md): one canonical entry, an explicit
adapter for both writer styles at the engine/platform boundary, and a
recorded stage 0 run in which the Sonic Advance 1 stage HUD is at its
authored positions without the compile-time switch. The upstream port is
unchanged, so the upstream build still shows the corrupted HUD.

The upstream source already flags the area: `include/gba/types.h:100` says
`TODO: EXTENDED_OAM is not yet functional`.

## Root cause 3: the BG-sprite backdrop is never composed

Character select is not a widescreen failure. It is already corrupt at a clean
240x160 build ([before](images/sa1-ui-character-select-native-240x160-before.png)).

The backdrop is animation 723: 600 tiles (19,200 bytes, one 240x160 4bpp
image) uploaded to `BG_CHAR_ADDR_FROM_BGCNT(1) + 0x40`, i.e. tile 2 at
`0x4040`. `Task_805B984` calls `UpdateSpriteAnimation_BG` and
`DisplaySprite_BG` every frame (`character_select.c:1561-1578`). The tile
upload itself goes through `animCmd_GetTiles_BG` (`src/background.c:767-791`),
so the image is in VRAM.

What is missing is the BG map. Two port-side gaps combine:

- `sub_8004010` (`src/background.c:1058`) is the VBlank fill that should write
  the text-BG map for `gBgSprites`. Its C implementation is a non-matching
  decompilation (58.36% in the upstream scratch link) and the fill in its
  `gBgSprites_Unknown2[bg][2] == 0xFF` branch is disabled with `#if 0` and a
  comment that it crashes the game (`background.c:1121-1125`).
- `sub_80039E4` (`src/background.c:865`) is the per-frame BG-sprite compositor.
  Under `PORTABLE` it clears `gBgSpritesCount` and returns before doing any
  work (`background.c:878-881`). The software path below it is also marked
  unfinished.

The BG1 map at screen base 25 therefore still contains whatever the previous
screen left there. The animation tiles are present but unreferenced. Filling
the 32x32 BG1 map with tile 2 (the animation's base tile) restores the correct
backdrop at 240x160 ([after](images/sa1-ui-character-select-native-240x160-after.png)).
At 426x240 the same screen is clean when the UI viewport is applied
([after](images/sa1-ui-character-select-426-after.png)).

The local fill is evidence, not a fix: it hardcodes both the tile number and
the screen. The proper implementation is the BG-sprite compositor, which
derives map entries from the sprite and writes them per frame (follow-up #16).
Staff credits uses the same `gBgCntRegs[1] = 0x1907` setup
(`src/game/sa1/ui/staff_credits.c:132-133`) and feeds the same
`gBgSprites_Unknown*` arrays directly (`:205-209`) instead of calling
`DisplaySprite_BG`, so it probably shares the gap; it was not captured in this
investigation.

## Experiments

All experiments were made in a private checkout. `docs/CAPTURE.md` documents the
capture hook and the scripted SA1 input. The before tree
(`/tmp/opencode/sa2_before`) is the pinned commit with only that hook; the after
tree (`/tmp/opencode/sa2_after`) adds the local changes described below.

| Experiment | Local change | Scene | Result | Limits |
| :--- | :--- | :--- | :--- | :--- |
| A. UI viewport | `compose_ui_viewport()` in `gpsp_renderer.cc`, enabled by `SA1_UI_VIEWPORT` | Title, zone select | Both clean, 1.5x centred with black pillars | Post-processes the already-rendered 426x240 buffer by cropping its left 240x160; applying it to gameplay crops the widescreen view |
| B. GBA OAM | `EXTENDED_OAM FALSE` in `defines.h` | Stage HUD | Rings, time and icon visible | Diagnostic only; changes the OAM ABI for every sprite |
| C. BG map fill | Fill the character-select BG1 map with tile 2 | Character select | Correct backdrop at 240x160; clean at 426 with A | Hardcodes tile 2 and the screen; does not implement BG-sprite composition |
| A+B+C | All three | Character select and gameplay | Character select clean at 426x240; gameplay HUD visible but cropped by A (the leak frame) | A is still global; the combined frame is a demonstration, not a design |

### A. UI viewport prototype

The local patch copies the left 240x160 of the 426x240 framebuffer, clears the
frame, and writes a 360x240 nearest-neighbour 1.5x image centred at x=33:

```c
static void compose_ui_viewport(u16 *framebuf)
{
    static u16 ui_frame[240 * 160];
    const int dst_w = 360, dst_h = 240, off_x = (DISPLAY_WIDTH - dst_w) / 2;
    int x, y;

    for (y = 0; y < 160; y++)
        for (x = 0; x < 240; x++)
            ui_frame[y * 240 + x] = framebuf[y * DISPLAY_WIDTH + x];

    memset(framebuf, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(u16));

    for (y = 0; y < dst_h; y++) {
        for (x = 0; x < dst_w; x++) {
            int sx = (x * 2) / 3;
            int sy = (y * 2) / 3;
            framebuf[y * DISPLAY_WIDTH + off_x + x] = ui_frame[sy * 240 + sx];
        }
    }
}
```

The after frames are
[title](images/sa1-ui-title-after.png) and
[zone select](images/sa1-ui-zone-select-after.png). The title frame is the
native art at 1.5x, not a new composition: the prototype only works because the
authored UI field happens to be the top-left 240x160 of the rendered frame.

Applied globally, the same patch crops gameplay to the 240x160 left region
([leak](images/sa1-ui-viewport-leak.png)). That frame is from the A+B+C run, so
it also carries the GBA OAM diagnostic and shows the HUD inside the crop. The
M2 renderer must gate the UI field on the scene, not on a global environment
variable or a post-process.

The 1.5x scale is a proof, not a presentation policy. At 426x240 it produces
uneven nearest-neighbour pixel widths; a shipping renderer should choose an
integer scale at the drawable size or a documented high-quality upscale.

### B. GBA OAM diagnostic

Changing `EXTENDED_OAM` to `FALSE` while keeping `WIDESCREEN_HACK TRUE` makes
the HUD appear ([before](images/sa1-ui-hud-before.png),
[after](images/sa1-ui-hud-after.png)). The rest of the 426x240 stage frame is
unchanged in the checked frames. This isolates the missing HUD to the OAM
layout, but it is not a viable design: the extended layout exists to carry
separate 16-bit sprite coordinates, and all writers must agree on one layout.

### C. Character-select map fill

The local fill writes tile 2 to every entry of the 32x32 BG1 map at the BGCNT
screen base. The before/after pair at 240x160 is
[before](images/sa1-ui-character-select-native-240x160-before.png) /
[after](images/sa1-ui-character-select-native-240x160-after.png). The 426x240
pair is
[before](images/sa1-ui-character-select-426-before.png) /
[after](images/sa1-ui-character-select-426-after.png).

The 426x240 "after" also uses the UI viewport and the GBA OAM diagnostic. It
demonstrates that the backdrop, viewport and HUD can coexist; it does not make
the fill or the OAM switch shipping code.

## What the M2 renderer must own

These are the invariants the remaster renderer needs so it does not inherit the
bugs above. They are requirements, not implemented behaviour.

1. **Coordinate space is explicit.** A frame is either a 240x160 UI field or
   the 426x240 gameplay world. The scene/menu boundary sets the mode. Do not
   infer it from map contents, a global environment variable, or the presence
   of a particular BG.
2. **Map geometry is bounded.** Honour BGCNT map size and the 32/64-tile
   stride. For a UI field, sample only the authored 240x160 region. For a
   512-wide map, address the second block correctly and do not keep the
   SA2-specific "disable the second block" hack in the shared path. Unowned
   map entries must not become visible stale VRAM.
3. **Viewport and letterbox policy are defined.** UI frames compose at 240x160
   and present with a uniform scale and bars. Gameplay stays full 426x240.
   Transitions must not leave a cropped gameplay frame behind. Prefer integer
   scaling where the drawable allows it; document the fractional fallback.
4. **One canonical OAM layout.** The renderer consumes `s16 x/y` plus the GBA
   attribute words. GBA-native `.all` writers and extended `.split` writers are
   adapted at the boundary. A compile-time switch that half the game ignores is
   not a valid design.
5. **BG-sprite layers are composed.** Implement the `sub_80039E4` semantics (or
   an equivalent) so `DisplaySprite_BG` produces a map as well as tiles.
   Character select and staff credits depend on it. Do not hardcode per-screen
   tile numbers or map addresses.
6. **VRAM ownership is visible.** A UI field that only authors 30x20 tiles must
   not display rows 20-31. Clear, clamp, or compose the field before sampling.
7. **Both games stay green.** The 512-wide handling and the OAM adapter must be
   checked against SA2 widescreen frames as well as the SA1 frames here. A fix
   for SA1 that re-breaks SA2 is not a fix.

The follow-up implementation work is split so each invariant has an owner:
[#15](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/15) for the
UI viewport and map bounds, [#14](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/14)
for the OAM layout, and [#16](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/16)
for BG-sprite composition. All three build on the M2 stage 0 host, #13.

## What remains open

- No fix is shipped. The after frames are from local experiments, not from the
  repository's renderer.
- The character-select compositor is not implemented; the map fill is a
  stand-in. Staff credits and other BG-sprite screens were not captured.
- The 512-wide text-BG path was not tested in isolation. The title's visible
  repeat is explained by the 256-wide BG0 wrap; the title BG2 and the SA2 hack
  need a dedicated parity test.
- Window and blend effects were not investigated. Character select uses both
  (`gWinRegs`, `gBldRegs`), and the native frame may still differ from hardware
  at the edges.
- The HUD adapter design is unresolved: convert all writers to the canonical
  layout, or keep two buffers and sync at VBlank. The frames only establish
  that the layout mismatch is the cause.
- The 1.5x nearest-neighbour viewport is a proof. The final scale policy, and
  whether SA1 UI art is ever expanded beyond 240x160, is an M2 decision.
- The character-select banner reads `HARACTER ... SELECT A` at 240x160 because
  the art is wider than the field. That is native output, not a port artifact;
  it was not investigated as a separate issue.

## Reproduction

### Build and capture before

```sh
git clone https://github.com/SAT-R/sa2 /tmp/opencode/sa2_before
cd /tmp/opencode/sa2_before
git checkout 393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d
cp "/path/to/Sonic Advance (Europe) (En,Ja,Fr,De,Es).gba" baserom_sa1.gba
# apply the local capture hook from docs/CAPTURE.md to
# src/platform/pret_sdl/sdl2.c
make -j"$(nproc)" sdl GAME_NAME=sa1
mkdir -p /tmp/opencode/cap_sa1_before_clean
HEADLESS=true \
  CAPTURE_DIR=/tmp/opencode/cap_sa1_before_clean \
  CAPTURE_SCRIPT=sa1 \
  CAPTURE_EVERY=10 \
  CAPTURE_MAX=1600 \
  timeout 240 ./sa1.sdl
```

The timeout exit status is expected: `CAPTURE_MAX` stops writing frames, not
the game.

### Build and capture after

The after tree starts from the same checkout and capture hook, then adds:

- the `compose_ui_viewport()` prototype in `gpsp_renderer.cc`, gated by
  `SA1_UI_VIEWPORT`;
- `#define EXTENDED_OAM FALSE` in `include/gba/defines.h` for the HUD
  diagnostic;
- the character-select BG1 map fill in
  `src/game/sa1/ui/character_select.c`.

```sh
cd /tmp/opencode/sa2_after
make -j"$(nproc)" sdl GAME_NAME=sa1

# Title and zone select with the UI viewport only.
mkdir -p /tmp/opencode/cap_sa1_after_viewport
SA1_UI_VIEWPORT=1 HEADLESS=true \
  CAPTURE_DIR=/tmp/opencode/cap_sa1_after_viewport \
  CAPTURE_SCRIPT=sa1 CAPTURE_EVERY=10 CAPTURE_MAX=1600 \
  timeout 240 ./sa1.sdl

# HUD with the GBA OAM layout, no viewport.
mkdir -p /tmp/opencode/cap_sa1_after_hud
HEADLESS=true \
  CAPTURE_DIR=/tmp/opencode/cap_sa1_after_hud \
  CAPTURE_SCRIPT=sa1 CAPTURE_EVERY=10 CAPTURE_MAX=1600 \
  timeout 240 ./sa1.sdl

# Character select with all three experiments.
mkdir -p /tmp/opencode/cap_sa1_after_all
SA1_UI_VIEWPORT=1 HEADLESS=true \
  CAPTURE_DIR=/tmp/opencode/cap_sa1_after_all \
  CAPTURE_SCRIPT=sa1 CAPTURE_EVERY=10 CAPTURE_MAX=1600 \
  timeout 240 ./sa1.sdl
```

The frames used here are VBlank 800 (title), 1000 (character select), 1100
(zone select) and 1400 (stage HUD).

### Native 240x160 control

A second copy of the pinned tree set `DISPLAY_WIDTH 240` and
`DISPLAY_HEIGHT 160` in `include/gba/defines.h`, kept the capture hook, and
built with the same `make -j"$(nproc)" sdl GAME_NAME=sa1`. The clean run is
`/tmp/opencode/cap_sa1_native_clean`; the map-fill run adds the character-select
fill and is `/tmp/opencode/cap_sa1_native_fill`. Frames were converted with:

```sh
python3 tools/ppm_to_png.py FRAME.ppm docs/images/NAME.png --scale 2   # 426x240
python3 tools/ppm_to_png.py FRAME.ppm docs/images/NAME.png --scale 3   # 240x160
```

The converter uses nearest-neighbour scaling only.

## References

- Upstream `SAT-R/sa2` at `393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d`.
- `src/game/sa1/menus/title_screen.c:341-425`
- `src/game/sa1/ui/course_select.c:111-132`, `:148`, `:211-213`
- `src/game/sa1/ui/character_select.c:116`, `:234-253`, `:1561-1578`
- `src/game/sa1/ui/staff_credits.c:114`, `:132-133`, `:205-209`
- `src/game/sa1/stage/stage_ui.c:304-342`
- `src/game/sa1/stage/ui_rendering.c:150-199`, `:317-380`, `:383-446`
- `src/background.c:767-791`, `:821`, `:865-1026`, `:1058-1142`
- `src/core.c:228-236`
- `src/platform/pret_sdl/sdl2.c:296-400`, `:673-678`
- `src/platform/shared/video/gpsp_renderer.cc:145`, `:341-482`, `:486-605`,
  `:1433-1516`, `:1524-1619`, `:2286`
- `include/gba/defines.h:45-75`
- `include/gba/types.h:100-264`
- [Issue #11](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/11)
- [Upstream issue #274](https://github.com/SAT-R/sa2/issues/274)
- [M2 stage 0 host #13](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/13)
- [OAM layout follow-up #14](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/14)
- [UI viewport follow-up #15](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/15)
- [BG-sprite follow-up #16](https://github.com/NahuelGenchi/sonic-advance-remaster/issues/16)
- [Renderer architecture](RENDERER.md)
- [Frame capture](CAPTURE.md)
