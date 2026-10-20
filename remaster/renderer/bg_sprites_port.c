/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapter between the upstream game state and the project-owned BG-sprite
 * composition module. See bg_sprites_port.h for the VBlank order.
 */

#include "renderer/bg_sprites_port.h"

#include "global.h"
#include "core.h"
#include "sprite.h"
#include "background.h"
#include "gba/types.h"

static SarBgFrame s_frame;
static SarBgSubframe s_subframes[SAR_BG_SPRITE_MAX][SAR_BG_SUBFRAME_MAX];
static SarBgStats s_stats;

/* The character base of a layer, as a byte offset inside BG VRAM. */
static uint32_t SarBgCharBaseOffset(int bg)
{
    return (uint32_t)(gBgCntRegs[bg] & BGCNT_CHARBASE(0x3)) << 12;
}

/* Is a sprite's character data inside the layer's character base? */
static int SarBgDestInRange(int bg, const void *dest)
{
    const uint8_t *start = (const uint8_t *)BG_VRAM + SarBgCharBaseOffset(bg);
    const uint8_t *end = start + 0x4000;
    const uint8_t *value = (const uint8_t *)dest;

    return value >= start && value < end;
}

void SarBgSpritesBeginVBlank(void)
{
    u8 count;
    u8 i;

    SarBgFrameInit(&s_frame);

    for (i = 0; i < SAR_BG_LAYER_COUNT; i++) {
        s_frame.bgcnt[i] = gBgCntRegs[i];
        s_frame.fill_tile[i] = gBgSprites_Unknown1[i];
        s_frame.fill_start_col[i] = gBgSprites_Unknown2[i][0];
        s_frame.fill_start_row[i] = gBgSprites_Unknown2[i][1];
        s_frame.fill_end_col[i] = gBgSprites_Unknown2[i][2];
        s_frame.fill_end_row[i] = gBgSprites_Unknown2[i][3];
    }
    s_frame.dispcnt = gDispCnt;

    count = gBgSpritesCount;
    if (count > SAR_BG_SPRITE_MAX) {
        count = SAR_BG_SPRITE_MAX;
    }

    for (i = 0; i < count; i++) {
        const Sprite *sprite = gBgSprites[i];
        const SpriteOffset *dimensions;
        SarBgSprite *out;
        const OamDataShort *oam;
        SarBgSubframe *subframes;
        u16 subframe_count;
        u16 j;

        if (sprite == NULL) {
            continue;
        }

        dimensions = sprite->dimensions;
        if (dimensions == (const SpriteOffset *)(void *)-1) {
            continue;
        }

        out = &s_frame.sprites[s_frame.sprite_count];
        subframes = s_subframes[s_frame.sprite_count];
        out->bg_id = (u8)SPRITE_FLAG_GET(sprite, BG_ID);
        out->x = sprite->x;
        out->y = sprite->y;
        out->offset_x = dimensions->offsetX;
        out->offset_y = dimensions->offsetY;
        out->width = (int16_t)dimensions->width;
        out->height = (int16_t)dimensions->height;
        out->flip = dimensions->flip;
        out->x_flip = (u8)SPRITE_FLAG_GET(sprite, X_FLIP);
        out->y_flip = (u8)SPRITE_FLAG_GET(sprite, Y_FLIP);
        out->tile_shift = (gBgCntRegs[out->bg_id] & BGCNT_256COLOR) ? 6 : 5;

        if (!SarBgDestInRange(out->bg_id, sprite->graphics.dest)) {
            /* The character data is not in this layer's character base. */
            continue;
        }

        out->tile_base = (u32)(((const u8 *)sprite->graphics.dest - (const u8 *)BG_VRAM - SarBgCharBaseOffset(out->bg_id))
                               >> out->tile_shift);

        /*
         * The stride of the OAM subframe table is the size of one OamDataShort
         * entry. It is 6 bytes in the GBA-native layout and 10 bytes in the
         * extended (widescreen) layout, so it must not be hardcoded.
         */
        oam = (const OamDataShort *)(const void *)&gRefSpriteTables->oamData[sprite->graphics.anim]
                                                                                 [(size_t)dimensions->oamIndex
                                                                                      * (sizeof(OamDataShort) / sizeof(u16))];

        subframe_count = dimensions->numSubframes;
        if (subframe_count > SAR_BG_SUBFRAME_MAX) {
            subframe_count = SAR_BG_SUBFRAME_MAX;
        }

        for (j = 0; j < subframe_count; j++) {
            const OamDataShort *entry = &oam[j];
            SarBgSubframe *subframe = &subframes[j];
            u8 shape_and_size = (u8)((entry->shape << 2) | entry->size);

            subframe->width = (u16)(gOamShapesSizes[shape_and_size][0] >> 3) * 8;
            subframe->height = (u16)(gOamShapesSizes[shape_and_size][1] >> 3) * 8;
            subframe->x = (int16_t)entry->x;
            subframe->y = (int16_t)entry->y;
            subframe->tile = entry->tileNum;
            subframe->palette = (u8)((entry->paletteNum + sprite->palId) & 0xF);
            subframe->hflip = (entry->matrixNum & ST_OAM_HFLIP) ? 1 : 0;
            subframe->vflip = (entry->matrixNum & ST_OAM_VFLIP) ? 1 : 0;
        }

        out->subframes = subframes;
        out->subframe_count = subframe_count;
        s_frame.sprite_count++;
    }
}

void SarBgSpritesCompose(void)
{
    SarBgComposeFrame(&s_frame, (u8 *)BG_VRAM, &s_stats);
}

const SarBgStats *SarBgSpritesStats(void)
{
    return &s_stats;
}
