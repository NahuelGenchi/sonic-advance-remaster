/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Project-owned BG-sprite composition for the M2 renderer.
 *
 * The upstream GBA engine composes "BG sprites" (full-screen images that are
 * uploaded as character data and then referenced from a text background map)
 * in two places:
 *
 *   - sub_80039E4 (src/background.c) writes the per-frame map entries of every
 *     registered BG sprite. Upstream stubs it out under PORTABLE.
 *   - sub_8004010 (src/background.c) fills rectangles of a text background map
 *     from the gBgSprites_Unknown1/Unknown2 state. Upstream's C version is an
 *     incomplete decompilation whose relevant fill branch is disabled.
 *
 * This module is the remaster equivalent of both. It owns no game state: it
 * reads a snapshot of the game's BG-sprite state and writes tilemap entries
 * into VRAM. The map entries are derived from the registered sprite (its
 * subframe layout, position, flips, palette and character base), never from a
 * per-screen tile number or map address.
 *
 * The module is deliberately free of upstream includes so that it can be
 * unit-tested on its own; the adapter that reads the upstream globals lives in
 * bg_sprites_port.c.
 */

#ifndef SAR_RENDERER_BG_SPRITES_H
#define SAR_RENDERER_BG_SPRITES_H

#include <stdint.h>

#define SAR_BG_LAYER_COUNT 4
#define SAR_BG_SPRITE_MAX 16
#define SAR_BG_SUBFRAME_MAX 32

/* Largest text map: 64x64 entries. */
#define SAR_BG_TEXT_MAP_MAX (64 * 64)

/* One OAM-sized subframe of a BG sprite. */
typedef struct {
    int16_t x;       /* subframe origin inside the sprite, in pixels */
    int16_t y;
    uint16_t width;  /* pixels */
    uint16_t height; /* pixels */
    uint16_t tile;   /* character number of the subframe's first tile */
    uint8_t palette; /* palette bank, 0-15 */
    uint8_t hflip;
    uint8_t vflip;
} SarBgSubframe;

/* One registered BG sprite. */
typedef struct {
    uint8_t bg_id;         /* background layer, 0-3 */
    int16_t x, y;          /* sprite position, in pixels */
    int16_t offset_x;      /* sprite anchor offset, in pixels */
    int16_t offset_y;
    int16_t width;         /* sprite size, in pixels */
    int16_t height;
    uint8_t flip;          /* anchor flip bits: bit 0 = X, bit 1 = Y */
    uint8_t x_flip;        /* sprite-level X flip */
    uint8_t y_flip;        /* sprite-level Y flip */
    uint32_t tile_base;    /* character number of the sprite's first tile */
    uint8_t tile_shift;    /* 5 for 4bpp, 6 for 8bpp */
    uint16_t subframe_count;
    const SarBgSubframe *subframes;
} SarBgSprite;

/* A snapshot of the game's BG-sprite state for one frame. */
typedef struct {
    uint16_t bgcnt[SAR_BG_LAYER_COUNT];
    uint16_t dispcnt;
    uint8_t fill_tile[SAR_BG_LAYER_COUNT];
    uint8_t fill_start_col[SAR_BG_LAYER_COUNT];
    uint8_t fill_start_row[SAR_BG_LAYER_COUNT];
    uint8_t fill_end_col[SAR_BG_LAYER_COUNT]; /* 0xFF: to the end of the row */
    uint8_t fill_end_row[SAR_BG_LAYER_COUNT]; /* exclusive */
    uint8_t sprite_count;
    SarBgSprite sprites[SAR_BG_SPRITE_MAX];
} SarBgFrame;

/* Geometry of one background layer's tilemap. */
typedef struct {
    int text;        /* 1: text map, 0: affine map */
    int width;       /* map width in tiles */
    int height;      /* map height in tiles */
    int stride;      /* entries per row */
    uint32_t offset; /* byte offset of the map inside BG VRAM */
} SarBgMapGeometry;

/* Counters for one composed frame. */
typedef struct {
    int layers_cleared;
    int fills_applied;
    int sprites_composed;
    int sprites_skipped;
    int entries_written;
    int entries_released;
} SarBgStats;

/* Reset a frame snapshot to "nothing registered". */
void SarBgFrameInit(SarBgFrame *frame);

/* Resolve the tilemap geometry of one layer. Returns 0 for an invalid layer. */
int SarBgMapGeometryFor(const SarBgFrame *frame, int bg, SarBgMapGeometry *geometry);

/* Forget which map entries the module owns. */
void SarBgResetOwnership(void);

/*
 * Apply the game's map fills and compose the registered BG sprites into
 * BG VRAM. `vram` points at the first byte of background VRAM.
 */
void SarBgComposeFrame(const SarBgFrame *frame, uint8_t *vram, SarBgStats *stats);

#endif /* SAR_RENDERER_BG_SPRITES_H */
