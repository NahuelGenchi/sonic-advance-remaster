/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The UI field: an explicit 240x160 coordinate space for the screens the
 * games authored on the GBA field, the text-background map sampler that stays
 * inside the authored map extents, and the presentation of the field next to
 * the 426x240 gameplay world.
 *
 * A frame is either
 *
 *   - the 426x240 gameplay world, which is what the stages are drawn for, or
 *   - the 240x160 UI field, which is what the Sonic Advance 1 menus are
 *     authored for.
 *
 * The coordinate space is a property of the scene, not of the frame's
 * contents: the scene is set at the screen/menu boundary and never inferred
 * from map contents, from an environment variable, from the presence of a
 * background layer or from a frame number. The scene state lives here; the
 * seam that reads it from the engine lives in `ui_viewport_port.c`.
 *
 * The field is composed from the engine state, not cropped out of a rendered
 * 426x240 frame. A screen that authors 30x20 tiles shows those 30x20 tiles
 * and nothing else: the map rows and columns the screen never wrote are
 * outside the field and are never sampled.
 *
 * The map sampler honours the BGCNT map size and the tile stride the maps
 * are written with. The games write their UI maps in the GBA layout (32x32
 * tile blocks, the second 256-pixel block of a 512-wide map in the next
 * 2 KB block); the widescreen level maps of the port use one linear strip of
 * 64 tiles per row. Both layouts are in the sampler, and the scene picks
 * one, so the 512-wide handling is a mode instead of a global hack.
 *
 * This module depends on nothing but the C standard library and the
 * project's own canonical OAM module.
 */

#ifndef SAR_RENDERER_UI_VIEWPORT_H
#define SAR_RENDERER_UI_VIEWPORT_H

#include <stdint.h>

#include "oam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The GBA field the Sonic Advance 1 UI screens are authored on. */
#define SAR_UI_FIELD_WIDTH  240
#define SAR_UI_FIELD_HEIGHT 160

/* The widescreen gameplay world. */
#define SAR_UI_WORLD_WIDTH  426
#define SAR_UI_WORLD_HEIGHT 240

/* The coordinate space a frame is composed in. */
typedef enum {
    /* The 426x240 gameplay world. */
    SAR_UI_SCENE_GAMEPLAY = 0,
    /* The 240x160 UI field. */
    SAR_UI_SCENE_FIELD = 1
} SarUiScene;

/* The layout a text background map is written in. */
typedef enum {
    /*
     * The GBA layout: 32x32 tile blocks, 32 tiles per row inside a block,
     * and the second 256-pixel block of a 512-wide map in the next 2 KB
     * block. The UI maps of both games are written this way.
     */
    SAR_UI_MAP_LAYOUT_GBA = 0,
    /*
     * The widescreen layout of the port's level maps: one linear strip of
     * 64 tiles per row, so the second 256-pixel block of a 512-wide map
     * follows the first one in the same row.
     */
    SAR_UI_MAP_LAYOUT_WIDE = 1
} SarUiMapLayout;

/* The geometry of a text background map, from its BGCNT size bits. */
typedef struct {
    uint32_t width;    /* 256 or 512 */
    uint32_t height;   /* 256 or 512 */
    uint32_t blocks_x; /* 1 or 2 */
    uint32_t blocks_y; /* 1 or 2 */
} SarUiMapGeometry;

/* One sampled point of a text background map. */
typedef struct {
    /* The map entry, in 16-bit entries from the map's base block. */
    int32_t entry;
    /* The sampled pixel inside the map, before the tile lookup. */
    uint32_t x;
    uint32_t y;
} SarUiMapSample;

/*
 * The state one field is composed from. The pointers stay valid for the
 * whole frame; the port adapter refreshes the registers per scanline.
 */
typedef struct {
    /* VRAM. */
    const uint8_t *vram;
    /* The palette: 512 entries, the first 256 are the background palettes. */
    const uint16_t *palette;
    /* The canonical OAM table of the frame. */
    const SarOamTable *oam;

    uint16_t dispcnt;
    uint16_t bgcnt[4];
    uint16_t bgofs_h[4];
    uint16_t bgofs_v[4];

    /* The affine background parameters, at the start of the frame. */
    int32_t bg2_x;
    int32_t bg2_y;
    int16_t bg2_pa;
    int16_t bg2_pb;
    int16_t bg2_pc;
    int16_t bg2_pd;
    int32_t bg3_x;
    int32_t bg3_y;
    int16_t bg3_pa;
    int16_t bg3_pb;
    int16_t bg3_pc;
    int16_t bg3_pd;
} SarUiViewportState;

/* The scene state. The screen/menu boundary sets it. */
void sar_ui_viewport_set_scene(SarUiScene scene);
SarUiScene sar_ui_viewport_get_scene(void);

/* The map geometry of a text background, from its BGCNT size bits. */
void sar_ui_map_geometry(uint16_t bgcnt, SarUiMapGeometry *geometry);

/*
 * Sample a point of a text background map. The scroll offsets are the
 * background's own scroll registers; the point is inside the field.
 */
void sar_ui_map_sample(const SarUiMapGeometry *geometry, SarUiMapLayout layout, int hofs, int vofs, int x, int y, SarUiMapSample *sample);

/* The map entry of a point, in 16-bit entries from the map's base block. */
int32_t sar_ui_map_entry(const SarUiMapGeometry *geometry, SarUiMapLayout layout, int hofs, int vofs, int x, int y);

/*
 * Compose one scanline of the field. The row holds SAR_UI_FIELD_WIDTH
 * pixels in the host's 16-bit pixel format. The affine background
 * references are stepped by the module, from the frame's start value.
 */
void sar_ui_viewport_render_scanline(const SarUiViewportState *state, SarUiMapLayout layout, int y, uint16_t *row);

/* Compose a whole field with one state, for the tests and for offline use. */
void sar_ui_viewport_render(const SarUiViewportState *state, SarUiMapLayout layout, uint16_t *field);

/*
 * Present a field inside a world-sized frame: one uniform scale, letterboxed
 * and centred, never stretched and never cropped. The integer scale is used
 * when the frame allows it; the fractional fit is the documented fallback
 * for a frame smaller than the field.
 */
void sar_ui_viewport_present(const uint16_t *field, uint16_t *world, int world_width, int world_height);

#ifdef __cplusplus
}
#endif

#endif /* SAR_RENDERER_UI_VIEWPORT_H */
