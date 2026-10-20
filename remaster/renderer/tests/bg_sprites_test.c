/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Synthetic tests for the project-owned BG-sprite composition module.
 *
 * The tests drive bg_sprites.c directly, with hand-built frames, so they cover
 * the map geometry, the fills, the sprite composition (including flips and
 * palettes) and the release of stale entries without needing a ROM.
 *
 * Build and run:
 *   cc -std=c11 -Wall -Wextra -I remaster/renderer \
 *      remaster/renderer/bg_sprites.c remaster/renderer/tests/bg_sprites_test.c \
 *      -o /tmp/bg_sprites_test && /tmp/bg_sprites_test
 */

#include "bg_sprites.h"

#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond)                                                                                                                       \
    do {                                                                                                                                   \
        if (!(cond)) {                                                                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                        \
            s_failures++;                                                                                                                \
        }                                                                                                                                \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                                                        \
    do {                                                                                                                                   \
        long long a_ = (long long)(actual);                                                                                               \
        long long e_ = (long long)(expected);                                                                                             \
        if (a_ != e_) {                                                                                                                  \
            printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_);                                        \
            s_failures++;                                                                                                                \
        }                                                                                                                                \
    } while (0)

#define VRAM_SIZE 0x10000

static uint8_t s_vram[VRAM_SIZE];

static uint16_t *MapEntry(const SarBgMapGeometry *geometry, int row, int col)
{
    return (uint16_t *)(void *)(s_vram + geometry->offset) + (size_t)row * (size_t)geometry->stride + col;
}

/* A character-select-like backdrop: 240x160, tile base 2, three subframes. */
static void BuildBackdropSprite(SarBgSprite *sprite, SarBgSubframe *subframes)
{
    subframes[0].x = 0;
    subframes[0].y = 0;
    subframes[0].width = 64;
    subframes[0].height = 64;
    subframes[0].tile = 0;
    subframes[0].palette = 0;
    subframes[0].hflip = 0;
    subframes[0].vflip = 0;

    subframes[1].x = 64;
    subframes[1].y = 0;
    subframes[1].width = 64;
    subframes[1].height = 64;
    subframes[1].tile = 64;
    subframes[1].palette = 0;
    subframes[1].hflip = 0;
    subframes[1].vflip = 0;

    subframes[2].x = 0;
    subframes[2].y = 64;
    subframes[2].width = 64;
    subframes[2].height = 64;
    subframes[2].tile = 128;
    subframes[2].palette = 0;
    subframes[2].hflip = 0;
    subframes[2].vflip = 0;

    memset(sprite, 0, sizeof(*sprite));
    sprite->bg_id = 1;
    sprite->x = 0;
    sprite->y = 0;
    sprite->offset_x = 0;
    sprite->offset_y = 0;
    sprite->width = 240;
    sprite->height = 160;
    sprite->tile_base = 2;
    sprite->tile_shift = 5;
    sprite->subframes = subframes;
    sprite->subframe_count = 3;
}

static void TestGeometry(void)
{
    SarBgFrame frame;
    SarBgMapGeometry geometry;

    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240; /* mode 0: every layer is a text map */

    /* 32x32, screen base block 25 (character select: BGCNT 0x1907). */
    frame.bgcnt[1] = 0x1907;
    CHECK(SarBgMapGeometryFor(&frame, 1, &geometry));
    CHECK_EQ(geometry.text, 1);
    CHECK_EQ(geometry.width, 32);
    CHECK_EQ(geometry.height, 32);
    CHECK_EQ(geometry.stride, 32);
    CHECK_EQ(geometry.offset, 25u * 0x800u);

    /* 64x32 */
    frame.bgcnt[0] = 0x4000 | 0x0400;
    CHECK(SarBgMapGeometryFor(&frame, 0, &geometry));
    CHECK_EQ(geometry.width, 64);
    CHECK_EQ(geometry.height, 32);
    CHECK_EQ(geometry.stride, 64);

    /* 32x64 */
    frame.bgcnt[0] = 0x8000 | 0x0400;
    CHECK(SarBgMapGeometryFor(&frame, 0, &geometry));
    CHECK_EQ(geometry.width, 32);
    CHECK_EQ(geometry.height, 64);
    CHECK_EQ(geometry.stride, 32);

    /* 64x64 */
    frame.bgcnt[0] = 0xC000 | 0x0400;
    CHECK(SarBgMapGeometryFor(&frame, 0, &geometry));
    CHECK_EQ(geometry.width, 64);
    CHECK_EQ(geometry.height, 64);
    CHECK_EQ(geometry.stride, 64);

    /* Mode 1 makes layer 2 an affine map: 16..128 tiles square. */
    frame.dispcnt = 0x41;
    frame.bgcnt[2] = 0x4000;
    CHECK(SarBgMapGeometryFor(&frame, 2, &geometry));
    CHECK_EQ(geometry.text, 0);
    CHECK_EQ(geometry.width, 32);
    CHECK_EQ(geometry.height, 32);
    CHECK_EQ(geometry.stride, 32);

    frame.bgcnt[2] = 0xC000;
    CHECK(SarBgMapGeometryFor(&frame, 2, &geometry));
    CHECK_EQ(geometry.width, 128);
    CHECK_EQ(geometry.height, 128);
}

static void TestFill(void)
{
    SarBgFrame frame;
    SarBgMapGeometry geometry;
    SarBgStats stats;
    int row;
    int col;

    memset(s_vram, 0xCD, sizeof(s_vram));
    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240;
    frame.bgcnt[1] = 0x1907;
    frame.fill_tile[1] = 0;
    frame.fill_start_col[1] = 0;
    frame.fill_start_row[1] = 0;
    frame.fill_end_col[1] = 0xFF;
    frame.fill_end_row[1] = 0x20;

    CHECK(SarBgMapGeometryFor(&frame, 1, &geometry));
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(stats.fills_applied, 1);

    for (row = 0; row < 32; row++) {
        for (col = 0; col < 32; col++) {
            CHECK_EQ(*MapEntry(&geometry, row, col), 0);
        }
    }

    /* A partial rectangle: columns 2..9, rows 3..7 (end column exclusive). */
    memset(s_vram, 0xCD, sizeof(s_vram));
    frame.fill_tile[1] = 7;
    frame.fill_start_col[1] = 2;
    frame.fill_start_row[1] = 3;
    frame.fill_end_col[1] = 10;
    frame.fill_end_row[1] = 8;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(stats.fills_applied, 1);

    for (row = 0; row < 32; row++) {
        for (col = 0; col < 32; col++) {
            int inside = (row >= 3 && row < 8 && col >= 2 && col < 10);
            CHECK_EQ(*MapEntry(&geometry, row, col), inside ? 7 : 0xCDCD);
        }
    }

    /* An empty rectangle does nothing. */
    memset(s_vram, 0xCD, sizeof(s_vram));
    frame.fill_start_col[1] = 4;
    frame.fill_start_row[1] = 4;
    frame.fill_end_col[1] = 4;
    frame.fill_end_row[1] = 4;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(stats.fills_applied, 0);
    CHECK_EQ(*MapEntry(&geometry, 4, 4), 0xCDCD);
}

static void TestSpriteComposition(void)
{
    SarBgFrame frame;
    SarBgMapGeometry geometry;
    SarBgSprite sprite;
    SarBgSubframe subframes[3];
    SarBgStats stats;
    int col;

    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();

    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240;
    frame.bgcnt[1] = 0x1907;
    BuildBackdropSprite(&sprite, subframes);
    frame.sprites[0] = sprite;
    frame.sprite_count = 1;

    CHECK(SarBgMapGeometryFor(&frame, 1, &geometry));
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(stats.sprites_composed, 1);
    CHECK_EQ(stats.layers_cleared, 1);
    CHECK_EQ(stats.entries_written, 3 * 64);

    /* First subframe: tiles 2..9 across the first row of tiles. */
    for (col = 0; col < 8; col++) {
        CHECK_EQ(*MapEntry(&geometry, 0, col), 2 + col);
    }
    /* Second subframe starts at tile 2 + 64 and column 8. */
    CHECK_EQ(*MapEntry(&geometry, 0, 8), 2 + 64);
    CHECK_EQ(*MapEntry(&geometry, 0, 15), 2 + 71);
    /* Third subframe is one row down and starts at tile 2 + 128. */
    CHECK_EQ(*MapEntry(&geometry, 8, 0), 2 + 128);
    CHECK_EQ(*MapEntry(&geometry, 8, 7), 2 + 135);
    /* The tile id continues across the rows of a subframe. */
    CHECK_EQ(*MapEntry(&geometry, 1, 0), 2 + 8);

    /* Everything the sprite does not cover stays cleared. */
    CHECK_EQ(*MapEntry(&geometry, 0, 30), 0);
    CHECK_EQ(*MapEntry(&geometry, 20, 0), 0);
}

static void TestSpriteFlipsAndPalette(void)
{
    SarBgFrame frame;
    SarBgMapGeometry geometry;
    SarBgSprite sprite;
    SarBgSubframe subframes[3];
    SarBgStats stats;
    int i;

    /* X flip: the subframe is mirrored and every entry carries the X flip bit. */
    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();
    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240;
    frame.bgcnt[1] = 0x1907;
    BuildBackdropSprite(&sprite, subframes);
    sprite.x_flip = 1;
    frame.sprites[0] = sprite;
    frame.sprite_count = 1;
    CHECK(SarBgMapGeometryFor(&frame, 1, &geometry));
    SarBgComposeFrame(&frame, s_vram, &stats);

    /* The sprite is 240 wide, so the mirrored anchor lands on column 28. */
    CHECK_EQ(*MapEntry(&geometry, 0, 28) & 0x3FF, 2);
    CHECK_EQ(*MapEntry(&geometry, 0, 28) & 0x400, 0x400);
    CHECK_EQ(*MapEntry(&geometry, 0, 27) & 0x3FF, 3);
    CHECK_EQ(*MapEntry(&geometry, 0, 21) & 0x3FF, 9);
    /* The second subframe is mirrored into columns 20..13. */
    CHECK_EQ(*MapEntry(&geometry, 0, 20) & 0x3FF, 2 + 64);
    CHECK_EQ(*MapEntry(&geometry, 0, 13) & 0x3FF, 2 + 71);
    CHECK_EQ(*MapEntry(&geometry, 0, 12), 0);

    /* Y flip: the subframe is mirrored upwards and carries the Y flip bit. */
    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();
    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240;
    frame.bgcnt[1] = 0x1907;
    BuildBackdropSprite(&sprite, subframes);
    sprite.y_flip = 1;
    frame.sprites[0] = sprite;
    frame.sprite_count = 1;
    SarBgComposeFrame(&frame, s_vram, &stats);

    /* The sprite is 160 tall, so the mirrored anchor lands on row 19. */
    CHECK_EQ(*MapEntry(&geometry, 19, 0) & 0x3FF, 2);
    CHECK_EQ(*MapEntry(&geometry, 19, 0) & 0x800, 0x800);
    /* A subframe is 8 tiles wide, so its last row is 7 * 8 tiles further. */
    CHECK_EQ(*MapEntry(&geometry, 12, 0) & 0x3FF, 2 + 56);
    CHECK_EQ(*MapEntry(&geometry, 12, 0) & 0x800, 0x800);
    /* The third subframe is mirrored into rows 11..4. */
    CHECK_EQ(*MapEntry(&geometry, 11, 0) & 0x3FF, 2 + 128);
    CHECK_EQ(*MapEntry(&geometry, 4, 0) & 0x3FF, 2 + 128 + 56);
    CHECK_EQ(*MapEntry(&geometry, 3, 0), 0);

    /* Palette bits live in the top nibble of the map entry. */
    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();
    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240;
    frame.bgcnt[1] = 0x1907;
    BuildBackdropSprite(&sprite, subframes);
    for (i = 0; i < 3; i++) {
        subframes[i].palette = 3;
    }
    sprite.subframes = subframes;
    frame.sprites[0] = sprite;
    frame.sprite_count = 1;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(*MapEntry(&geometry, 0, 0) >> 12, 3);
    CHECK_EQ(*MapEntry(&geometry, 0, 0) & 0x3FF, 2);
}

static void TestStaleEntries(void)
{
    SarBgFrame frame;
    SarBgMapGeometry geometry;
    SarBgSprite sprite;
    SarBgSubframe subframes[3];
    SarBgStats stats;

    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();

    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240;
    frame.bgcnt[1] = 0x1907;
    BuildBackdropSprite(&sprite, subframes);
    frame.sprites[0] = sprite;
    frame.sprite_count = 1;
    CHECK(SarBgMapGeometryFor(&frame, 1, &geometry));

    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 2);

    /*
     * A sprite that moves: the layer is cleared every frame, so the entries
     * the sprite no longer covers do not survive. The compositor aligns the
     * sprite to a 16 pixel grid, so the move is a whole tile column.
     */
    frame.sprites[0].x = 16;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 0);
    CHECK_EQ(*MapEntry(&geometry, 0, 2), 2);
    CHECK_EQ(*MapEntry(&geometry, 1, 2), 2 + 8);
    CHECK_EQ(*MapEntry(&geometry, 0, 10), 2 + 64);
    frame.sprites[0].x = 0;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 2);
    CHECK_EQ(*MapEntry(&geometry, 0, 2), 4);
    /* The entries of the moved position are gone again. */
    CHECK_EQ(*MapEntry(&geometry, 0, 18), 0);
    CHECK_EQ(*MapEntry(&geometry, 0, 17), 0);

    /*
     * The sprite is gone but the game left the map alone: the composition
     * stays, like on the hardware.
     */
    frame.sprite_count = 0;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(stats.entries_released, 0);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 2);
    CHECK_EQ(*MapEntry(&geometry, 8, 0), 2 + 128);

    /*
     * The game has taken the layer over and written its own map: the entries
     * this module still owns are stale and are released, and the entry the
     * game wrote is kept.
     */
    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();
    frame.sprite_count = 1;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 2);
    *MapEntry(&geometry, 0, 0) = 0x1234;
    frame.sprite_count = 0;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(stats.entries_released, 3 * 64 - 1);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 0x1234);
    CHECK_EQ(*MapEntry(&geometry, 0, 1), 0);

    /* A layer that moves to another map base is not cleared. */
    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();
    frame.sprite_count = 1;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 2);
    frame.bgcnt[1] = 0x1907 - 0x100; /* another screen base */
    frame.sprite_count = 0;
    SarBgComposeFrame(&frame, s_vram, &stats);
    CHECK_EQ(*MapEntry(&geometry, 0, 0), 2);
}

static void TestMultipleLayersAndSizes(void)
{
    SarBgFrame frame;
    SarBgMapGeometry geometry0;
    SarBgMapGeometry geometry1;
    SarBgSprite sprite0;
    SarBgSprite sprite1;
    SarBgSubframe subframes0[3];
    SarBgSubframe subframes1[1];
    SarBgStats stats;
    int row;
    int col;

    memset(s_vram, 0, sizeof(s_vram));
    SarBgResetOwnership();

    SarBgFrameInit(&frame);
    frame.dispcnt = 0x1240;

    /* Layer 0: a 64x32 map. Layer 1: a 32x32 map. */
    frame.bgcnt[0] = 0x4000 | 0x0100;
    frame.bgcnt[1] = 0x1907;

    BuildBackdropSprite(&sprite0, subframes0);
    sprite0.bg_id = 0;
    sprite0.tile_base = 100;

    memset(&sprite1, 0, sizeof(sprite1));
    sprite1.bg_id = 1;
    sprite1.width = 16;
    sprite1.height = 16;
    sprite1.tile_base = 5;
    subframes1[0].x = 0;
    subframes1[0].y = 0;
    subframes1[0].width = 16;
    subframes1[0].height = 16;
    subframes1[0].tile = 0;
    subframes1[0].palette = 1;
    sprite1.subframes = subframes1;
    sprite1.subframe_count = 1;

    frame.sprites[0] = sprite0;
    frame.sprites[1] = sprite1;
    frame.sprite_count = 2;

    CHECK(SarBgMapGeometryFor(&frame, 0, &geometry0));
    CHECK(SarBgMapGeometryFor(&frame, 1, &geometry1));
    SarBgComposeFrame(&frame, s_vram, &stats);

    CHECK_EQ(stats.sprites_composed, 2);
    CHECK_EQ(stats.layers_cleared, 2);

    /* The 64-wide map keeps its own stride. */
    CHECK_EQ(*MapEntry(&geometry0, 0, 0), 100);
    CHECK_EQ(*MapEntry(&geometry0, 0, 8), 100 + 64);
    CHECK_EQ(*MapEntry(&geometry0, 1, 0), 100 + 8);
    CHECK_EQ(*MapEntry(&geometry0, 0, 32), 0);

    /* The second layer is composed independently. */
    CHECK_EQ(*MapEntry(&geometry1, 0, 0), 5 | (1 << 12));
    CHECK_EQ(*MapEntry(&geometry1, 0, 1), 6 | (1 << 12));
    CHECK_EQ(*MapEntry(&geometry1, 1, 0), 7 | (1 << 12));
    CHECK_EQ(*MapEntry(&geometry1, 0, 2), 0);

    for (row = 0; row < 32; row++) {
        for (col = 0; col < 32; col++) {
            if (row < 2 && col < 2) {
                continue;
            }
            CHECK_EQ(*MapEntry(&geometry1, row, col), 0);
        }
    }
}

int main(void)
{
    TestGeometry();
    TestFill();
    TestSpriteComposition();
    TestSpriteFlipsAndPalette();
    TestStaleEntries();
    TestMultipleLayersAndSizes();

    if (s_failures != 0) {
        printf("bg_sprites_test: %d failure(s)\n", s_failures);
        return 1;
    }

    printf("bg_sprites_test: all checks passed\n");
    return 0;
}
