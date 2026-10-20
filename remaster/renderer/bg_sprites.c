/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Project-owned BG-sprite composition. See bg_sprites.h for the contract.
 *
 * Rules implemented here, in the order the frame is built:
 *
 *   1. A layer that no longer carries a BG sprite keeps its last composition
 *      while the game leaves the map alone: a screen can unregister its
 *      sprite for a frame without giving the layer away. When the game writes
 *      its own map over the layer, the entries this module still owns are
 *      stale and are released, so no entry of an earlier screen survives.
 *   2. A layer that does carry a BG sprite is owned by the compositor for
 *      that frame: its map is cleared, so no entry of an earlier screen and no
 *      entry of an earlier position of the sprite can survive in it.
 *   3. The fills the game requested (sub_8004010) are applied.
 *   4. The registered BG sprites are composed into the map (sub_80039E4).
 *
 * Fill rectangles use an exclusive end column and end row; an end column of
 * 0xFF means "to the end of the row". That matches every rectangle the two
 * games request: a full 32x32 map is {0, 0, 0xFF, 32}.
 */

#include "bg_sprites.h"

#include <string.h>

/* Mirrors the portable build's BGCNT_SCREENBASE_MASK (widescreen VRAM). */
#define SAR_BG_SCREENBASE_MASK 0x3F00u

/* One owned map entry: the value the module wrote, if it still owns it. */
typedef struct {
    uint16_t value;
    uint8_t valid;
} SarBgOwnedEntry;

static SarBgOwnedEntry s_owned[SAR_BG_LAYER_COUNT][SAR_BG_TEXT_MAP_MAX];
static SarBgMapGeometry s_geometry[SAR_BG_LAYER_COUNT];
static uint8_t s_geometry_valid[SAR_BG_LAYER_COUNT];

void SarBgFrameInit(SarBgFrame *frame)
{
    if (frame == NULL) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
}

void SarBgResetOwnership(void)
{
    memset(s_owned, 0, sizeof(s_owned));
    memset(s_geometry, 0, sizeof(s_geometry));
    memset(s_geometry_valid, 0, sizeof(s_geometry_valid));
}

/* Text maps are 32x32, 64x32, 32x64 or 64x64; affine maps are 16..128 square. */
static int SarBgLayerIsAffine(int bg, uint16_t dispcnt)
{
    switch (dispcnt & 3) {
    case 1:
        return bg == 2;
    case 2:
        return bg == 3;
    default:
        return 0;
    }
}

int SarBgMapGeometryFor(const SarBgFrame *frame, int bg, SarBgMapGeometry *geometry)
{
    uint16_t bgcnt;
    uint32_t base_block;
    int size;

    if (frame == NULL || geometry == NULL || bg < 0 || bg >= SAR_BG_LAYER_COUNT) {
        return 0;
    }

    bgcnt = frame->bgcnt[bg];
    base_block = (bgcnt & SAR_BG_SCREENBASE_MASK) >> 8;
    size = (bgcnt >> 14) & 3;

    geometry->offset = base_block * 0x800u;

    if (SarBgLayerIsAffine(bg, frame->dispcnt)) {
        geometry->text = 0;
        geometry->width = 16 << size;
        geometry->height = 16 << size;
    } else {
        geometry->text = 1;
        geometry->width = (size & 1) ? 64 : 32;
        geometry->height = (size & 2) ? 64 : 32;
    }

    geometry->stride = geometry->width;
    return 1;
}

static int SarBgGeometryEqual(const SarBgMapGeometry *a, const SarBgMapGeometry *b)
{
    return a->text == b->text && a->width == b->width && a->height == b->height && a->stride == b->stride
           && a->offset == b->offset;
}

static uint16_t *SarBgMapBase(const SarBgMapGeometry *geometry, uint8_t *vram)
{
    return (uint16_t *)(void *)(vram + geometry->offset);
}

static uint16_t *SarBgMapEntry(const SarBgMapGeometry *geometry, uint8_t *vram, int row, int col)
{
    return SarBgMapBase(geometry, vram) + (size_t)row * (size_t)geometry->stride + col;
}

/* Release the entries this module owns on a layer that has no BG sprite. */
static void SarBgReleaseLayer(int bg, const SarBgMapGeometry *current, uint8_t *vram, SarBgStats *stats)
{
    int index;
    int total;
    int taken_over = 0;

    if (!s_geometry_valid[bg]) {
        return;
    }

    if (!SarBgGeometryEqual(&s_geometry[bg], current)) {
        /* The map moved or changed shape: the old entries are not ours. */
        memset(s_owned[bg], 0, sizeof(s_owned[bg]));
        return;
    }

    total = current->width * current->height;

    /*
     * A layer that stops carrying a BG sprite keeps its last composition,
     * like on the hardware: a screen can unregister its sprite for a frame
     * without giving the layer away. The layer is given away when the game
     * writes its own map over the layer, and then the entries this module
     * still owns are stale and are released.
     */
    for (index = 0; index < SAR_BG_TEXT_MAP_MAX; index++) {
        const uint16_t *entry;

        if (!s_owned[bg][index].valid || index >= total) {
            continue;
        }

        entry = SarBgMapEntry(current, vram, index / current->stride, index % current->stride);
        if (*entry != s_owned[bg][index].value) {
            taken_over = 1;
            break;
        }
    }

    for (index = 0; index < SAR_BG_TEXT_MAP_MAX; index++) {
        uint16_t *entry;

        if (!s_owned[bg][index].valid) {
            continue;
        }
        s_owned[bg][index].valid = 0;

        if (!taken_over || index >= total) {
            continue;
        }

        entry = SarBgMapEntry(current, vram, index / current->stride, index % current->stride);
        if (*entry == s_owned[bg][index].value) {
            *entry = 0;
            if (stats != NULL) {
                stats->entries_released++;
            }
        }
    }
}

/* A layer with a BG sprite belongs to the compositor: clear the whole map. */
static void SarBgClearLayer(int bg, const SarBgMapGeometry *geometry, uint8_t *vram, SarBgStats *stats)
{
    int total = geometry->width * geometry->height;

    memset(SarBgMapBase(geometry, vram), 0, (size_t)total * sizeof(uint16_t));
    memset(s_owned[bg], 0, sizeof(s_owned[bg]));

    if (stats != NULL) {
        stats->layers_cleared++;
    }
}

/* The text-BG map fill of sub_8004010. */
static void SarBgApplyFill(int bg, const SarBgFrame *frame, const SarBgMapGeometry *geometry, uint8_t *vram, SarBgStats *stats)
{
    int start_col = frame->fill_start_col[bg];
    int start_row = frame->fill_start_row[bg];
    int end_col = frame->fill_end_col[bg];
    int end_row = frame->fill_end_row[bg];
    uint16_t value = frame->fill_tile[bg];
    int row;

    if (end_col == 0xFF) {
        end_col = geometry->width;
    }

    if (end_col <= start_col || end_row <= start_row) {
        return;
    }

    if (start_col < 0) {
        start_col = 0;
    }
    if (start_row < 0) {
        start_row = 0;
    }
    if (end_col > geometry->width) {
        end_col = geometry->width;
    }
    if (end_row > geometry->height) {
        end_row = geometry->height;
    }

    for (row = start_row; row < end_row; row++) {
        uint16_t *map = SarBgMapBase(geometry, vram) + (size_t)row * (size_t)geometry->stride;
        int col;

        for (col = start_col; col < end_col; col++) {
            map[col] = value;
        }
    }

    if (stats != NULL) {
        stats->fills_applied++;
    }
}

/* The per-frame BG-sprite compositor of sub_80039E4. */
static void SarBgComposeSprite(int bg, const SarBgSprite *sprite, const SarBgMapGeometry *geometry, uint8_t *vram, SarBgStats *stats)
{
    int flip_x;
    int flip_y;
    int32_t base_x;
    int32_t base_y;
    int i;

    if (sprite->subframes == NULL || sprite->subframe_count == 0) {
        if (stats != NULL) {
            stats->sprites_skipped++;
        }
        return;
    }

    if (!geometry->text) {
        /* Affine BG sprites are not composed; see docs/BG-SPRITES.md. */
        if (stats != NULL) {
            stats->sprites_skipped++;
        }
        return;
    }

    flip_x = (sprite->x_flip != (sprite->flip & 1)) ? 1 : 0;
    flip_y = (sprite->y_flip ^ ((sprite->flip >> 1) & 1)) ? 1 : 0;

    if (flip_x) {
        base_x = sprite->x + ((sprite->flip & 1) ? sprite->offset_x : (sprite->width - sprite->offset_x)) - 8;
    } else {
        base_x = sprite->x - sprite->offset_x;
    }

    if (flip_y) {
        base_y = sprite->y + ((sprite->flip & 1) ? sprite->offset_y : (sprite->height - sprite->offset_y)) - 8;
    } else {
        base_y = sprite->y - sprite->offset_y;
    }

    /* The compositor aligns the sprite to a 16x8 pixel grid. */
    base_x &= ~15;
    base_y &= ~7;

    for (i = 0; i < sprite->subframe_count; i++) {
        const SarBgSubframe *frame = &sprite->subframes[i];
        int tiles_x = frame->width / 8;
        int tiles_y = frame->height / 8;
        int hflip = ((frame->hflip != 0) ? 1 : 0) ^ flip_x;
        int vflip = ((frame->vflip != 0) ? 1 : 0) ^ flip_y;
        int32_t sub_x = frame->x;
        int32_t sub_y = frame->y;
        int32_t col0;
        int32_t row0;
        int tx;
        int ty;

        if (tiles_x <= 0 || tiles_y <= 0) {
            continue;
        }

        if (flip_x) {
            sub_x = -sub_x;
        }
        if (flip_y) {
            sub_y = -sub_y;
        }

        col0 = (base_x + sub_x) >> 3;
        row0 = (base_y + sub_y) >> 3;

        for (ty = 0; ty < tiles_y; ty++) {
            for (tx = 0; tx < tiles_x; tx++) {
                int32_t col = hflip ? (col0 - tx) : (col0 + tx);
                int32_t row = vflip ? (row0 - ty) : (row0 + ty);
                uint32_t tile = sprite->tile_base + frame->tile + (uint32_t)(ty * tiles_x + tx);
                uint16_t value;
                uint16_t *entry;
                int index;

                if (col < 0 || col >= geometry->width || row < 0 || row >= geometry->height) {
                    continue;
                }

                value = (uint16_t)(tile & 0x3FFu);
                value |= (uint16_t)((frame->palette & 0xF) << 12);
                if (hflip) {
                    value |= 0x400;
                }
                if (vflip) {
                    value |= 0x800;
                }

                entry = SarBgMapEntry(geometry, vram, (int)row, (int)col);
                *entry = value;

                index = (int)row * geometry->stride + (int)col;
                s_owned[bg][index].value = value;
                s_owned[bg][index].valid = 1;

                if (stats != NULL) {
                    stats->entries_written++;
                }
            }
        }
    }

    if (stats != NULL) {
        stats->sprites_composed++;
    }
}

void SarBgComposeFrame(const SarBgFrame *frame, uint8_t *vram, SarBgStats *stats)
{
    SarBgMapGeometry geometry[SAR_BG_LAYER_COUNT];
    uint8_t active[SAR_BG_LAYER_COUNT];
    int bg;
    int i;

    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
    if (frame == NULL || vram == NULL) {
        return;
    }

    memset(active, 0, sizeof(active));

    for (bg = 0; bg < SAR_BG_LAYER_COUNT; bg++) {
        (void)SarBgMapGeometryFor(frame, bg, &geometry[bg]);
    }

    for (i = 0; i < frame->sprite_count && i < SAR_BG_SPRITE_MAX; i++) {
        active[frame->sprites[i].bg_id & 3] = 1;
    }

    /* 1. Release the entries of layers that no longer carry a BG sprite. */
    for (bg = 0; bg < SAR_BG_LAYER_COUNT; bg++) {
        if (!active[bg]) {
            SarBgReleaseLayer(bg, &geometry[bg], vram, stats);
        }
    }

    /* 2. Layers with a BG sprite are owned by the compositor. */
    for (bg = 0; bg < SAR_BG_LAYER_COUNT; bg++) {
        if (active[bg] && geometry[bg].text) {
            SarBgClearLayer(bg, &geometry[bg], vram, stats);
        }
    }

    /* 3. The fills the game asked for. */
    for (bg = 0; bg < SAR_BG_LAYER_COUNT; bg++) {
        SarBgApplyFill(bg, frame, &geometry[bg], vram, stats);
    }

    /* 4. The registered BG sprites. */
    for (i = 0; i < frame->sprite_count && i < SAR_BG_SPRITE_MAX; i++) {
        const SarBgSprite *sprite = &frame->sprites[i];
        int id = sprite->bg_id & 3;

        SarBgComposeSprite(id, sprite, &geometry[id], vram, stats);
    }

    /* 5. Remember the geometry for the next frame. */
    for (bg = 0; bg < SAR_BG_LAYER_COUNT; bg++) {
        s_geometry[bg] = geometry[bg];
        s_geometry_valid[bg] = 1;
    }
}
