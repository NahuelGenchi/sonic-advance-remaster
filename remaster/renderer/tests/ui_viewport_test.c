/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Synthetic tests for the project-owned UI viewport module.
 *
 * The tests drive ui_viewport.c directly, with hand-built frames, so they
 * cover the map geometry, the two map layouts, the bounded field sampling
 * and the presentation without needing a ROM.
 *
 * Build and run:
 *   cc -std=c11 -Wall -Wextra -I remaster/renderer \
 *      remaster/renderer/ui_viewport.c remaster/renderer/tests/ui_viewport_test.c \
 *      -o /tmp/ui_viewport_test && /tmp/ui_viewport_test
 */

#include "ui_viewport.h"

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

#define VRAM_SIZE 0x1E000u
#define TILE_4BPP_SIZE 32u

static uint8_t s_vram[VRAM_SIZE];
static uint16_t s_palette[512];
static SarOamTable s_oam;

static uint16_t *MapEntry(uint16_t bgcnt, int32_t entry)
{
    return (uint16_t *)(void *)(s_vram + 0x800u * (uint32_t)((bgcnt >> 8) & 0x1Fu)) + entry;
}

static void TestMapGeometry(void)
{
    SarUiMapGeometry geometry;

    sar_ui_map_geometry(0x1480, &geometry);
    CHECK_EQ(geometry.width, 256);
    CHECK_EQ(geometry.height, 256);
    CHECK_EQ(geometry.blocks_x, 1);
    CHECK_EQ(geometry.blocks_y, 1);

    /* Size bits 1: 512 pixels wide. */
    sar_ui_map_geometry(0x5680, &geometry);
    CHECK_EQ(geometry.width, 512);
    CHECK_EQ(geometry.height, 256);
    CHECK_EQ(geometry.blocks_x, 2);
    CHECK_EQ(geometry.blocks_y, 1);

    /* Size bits 2: 512 pixels tall. */
    sar_ui_map_geometry(0x9800, &geometry);
    CHECK_EQ(geometry.width, 256);
    CHECK_EQ(geometry.height, 512);
    CHECK_EQ(geometry.blocks_x, 1);
    CHECK_EQ(geometry.blocks_y, 2);

    /* Size bits 3: 512 by 512. */
    sar_ui_map_geometry(0xD800, &geometry);
    CHECK_EQ(geometry.width, 512);
    CHECK_EQ(geometry.height, 512);
    CHECK_EQ(geometry.blocks_x, 2);
    CHECK_EQ(geometry.blocks_y, 2);
}

static void TestGbaLayout(void)
{
    SarUiMapGeometry geometry;

    /* A 256x256 map: 32 tiles per row, 32 rows. */
    sar_ui_map_geometry(0x1480, &geometry);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 0), 0);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 8, 0), 1);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 8), 32);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 248, 248), 31 * 32 + 31);

    /* A 512x256 map: the second 256-pixel block is the next 2 KB block. */
    sar_ui_map_geometry(0x5680, &geometry);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 0), 0);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 256, 0), 1024);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 248, 8), 32 + 31);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 256, 8), 1024 + 32);

    /* A 256x512 map: the second band is the next 2 KB block. */
    sar_ui_map_geometry(0x9800, &geometry);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 0), 0);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 256), 1024);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 248), 31 * 32);

    /* A 512x512 map: four blocks, the bottom band two blocks down. */
    sar_ui_map_geometry(0xD800, &geometry);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 0), 0);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 256, 0), 1024);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 256), 2048);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 256, 256), 3072);
}

static void TestWideLayout(void)
{
    SarUiMapGeometry geometry;

    /* A 512x256 map in the widescreen layout: one strip of 64 tiles per
     * row, so the second 256-pixel block follows the first one in the same
     * row. */
    sar_ui_map_geometry(0x5680, &geometry);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_WIDE, 0, 0, 0, 0), 0);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_WIDE, 0, 0, 256, 0), 32);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_WIDE, 0, 0, 0, 8), 64);

    /* A 256x256 map is the same in both layouts. */
    sar_ui_map_geometry(0x1480, &geometry);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_WIDE, 0, 0, 0, 8), 32);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_WIDE, 0, 0, 0, 0), 0);
}

/* The scroll offsets wrap the map the way the hardware does. */
static void TestScrollWrap(void)
{
    SarUiMapGeometry geometry;

    sar_ui_map_geometry(0x1480, &geometry);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 0, 256), 0);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, 256, 0), 0);
    CHECK_EQ(sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 8, 0, 0, 0), 1);
}

/*
 * A screen that authors 30x20 tiles in a 256x256 map must never sample the
 * rows and columns the screen did not write.
 */
static void TestBoundedField(void)
{
    SarUiMapGeometry geometry;
    int x;
    int y;

    sar_ui_map_geometry(0x1480, &geometry);

    for (y = 0; y < SAR_UI_FIELD_HEIGHT; y++) {
        for (x = 0; x < SAR_UI_FIELD_WIDTH; x++) {
            int32_t entry = sar_ui_map_entry(&geometry, SAR_UI_MAP_LAYOUT_GBA, 0, 0, x, y);

            CHECK(entry < 20 * 32);
        }
    }
}

static void SetUpState(SarUiViewportState *state, uint16_t bgcnt)
{
    memset(state, 0, sizeof(*state));
    state->vram = s_vram;
    state->palette = s_palette;
    state->oam = &s_oam;
    state->dispcnt = 0x0100;
    state->bgcnt[0] = bgcnt;
}

/* A text background of one tile colour fills the field. */
static void TestComposition(void)
{
    SarUiViewportState state;
    uint16_t field[SAR_UI_FIELD_WIDTH * SAR_UI_FIELD_HEIGHT];
    /* A 4bpp background at screen base 20, character base 0. */
    uint16_t bgcnt = 0x1400;
    int entry;
    int x;

    memset(s_vram, 0, sizeof(s_vram));
    memset(s_palette, 0, sizeof(s_palette));
    memset(&s_oam, 0, sizeof(s_oam));

    s_palette[0] = 0x1111;
    s_palette[1] = 0x2222;

    /* Every map entry points at tile 1, whose pixels are all colour 1. */
    for (entry = 0; entry < 32 * 32; entry++) {
        *MapEntry(bgcnt, entry) = 1;
    }
    for (x = 0; x < (int)TILE_4BPP_SIZE; x++) {
        s_vram[TILE_4BPP_SIZE + x] = 0x11;
    }

    SetUpState(&state, bgcnt);
    sar_ui_viewport_render(&state, SAR_UI_MAP_LAYOUT_GBA, field);

    CHECK_EQ(field[0], 0x2222);
    CHECK_EQ(field[SAR_UI_FIELD_WIDTH - 1], 0x2222);
    CHECK_EQ(field[(SAR_UI_FIELD_HEIGHT - 1) * SAR_UI_FIELD_WIDTH], 0x2222);
    CHECK_EQ(field[SAR_UI_FIELD_WIDTH * SAR_UI_FIELD_HEIGHT - 1], 0x2222);
}

/* A 512-wide map is addressed in two blocks, and the field reads the first. */
static void TestSecondBlock(void)
{
    SarUiViewportState state;
    uint16_t field[SAR_UI_FIELD_WIDTH * SAR_UI_FIELD_HEIGHT];
    /* A 4bpp 512x256 background at screen base 22. */
    uint16_t bgcnt = 0x5600;
    int entry;
    int x;

    memset(s_vram, 0, sizeof(s_vram));
    memset(s_palette, 0, sizeof(s_palette));
    memset(&s_oam, 0, sizeof(s_oam));

    s_palette[0] = 0x1111;
    s_palette[1] = 0x2222;
    s_palette[2] = 0x3333;

    /* The first block holds tile 1, the second block tile 2. */
    for (entry = 0; entry < 1024; entry++) {
        *MapEntry(bgcnt, entry) = 1;
    }
    for (entry = 1024; entry < 2048; entry++) {
        *MapEntry(bgcnt, entry) = 2;
    }
    for (x = 0; x < (int)TILE_4BPP_SIZE; x++) {
        s_vram[TILE_4BPP_SIZE + x] = 0x11;
        s_vram[2 * TILE_4BPP_SIZE + x] = 0x22;
    }

    SetUpState(&state, bgcnt);
    sar_ui_viewport_render(&state, SAR_UI_MAP_LAYOUT_GBA, field);

    /* The field is 240 pixels wide, so it only reads the first block. */
    CHECK_EQ(field[0], 0x2222);
    CHECK_EQ(field[SAR_UI_FIELD_WIDTH - 1], 0x2222);

    /* The same map in the widescreen layout reads the second block at the
     * second 256-pixel column, which the field does not reach either. */
    SetUpState(&state, bgcnt);
    sar_ui_viewport_render(&state, SAR_UI_MAP_LAYOUT_WIDE, field);
    CHECK_EQ(field[0], 0x2222);
    CHECK_EQ(field[SAR_UI_FIELD_WIDTH - 1], 0x2222);
}

/* The field is presented with one uniform scale and letterboxing. */
static void TestPresentation(void)
{
    uint16_t field[SAR_UI_FIELD_WIDTH * SAR_UI_FIELD_HEIGHT];
    uint16_t world[480 * 320];
    int x;
    int y;

    for (y = 0; y < SAR_UI_FIELD_HEIGHT; y++) {
        for (x = 0; x < SAR_UI_FIELD_WIDTH; x++) {
            field[y * SAR_UI_FIELD_WIDTH + x] = (uint16_t)(y * SAR_UI_FIELD_WIDTH + x);
        }
    }

    /* The 426x240 world: the integer scale is 1, so the field is centred with
     * black bars. */
    sar_ui_viewport_present(field, world, 426, 240);
    CHECK_EQ(world[0], 0);
    CHECK_EQ(world[40 * 426 + 93], field[0]);
    CHECK_EQ(world[40 * 426 + 93 + SAR_UI_FIELD_WIDTH - 1], field[SAR_UI_FIELD_WIDTH - 1]);
    CHECK_EQ(world[(40 + SAR_UI_FIELD_HEIGHT - 1) * 426 + 93], field[(SAR_UI_FIELD_HEIGHT - 1) * SAR_UI_FIELD_WIDTH]);
    CHECK_EQ(world[40 * 426 + 92], 0);
    CHECK_EQ(world[40 * 426 + 93 + SAR_UI_FIELD_WIDTH], 0);
    CHECK_EQ(world[39 * 426 + 93], 0);
    CHECK_EQ(world[(40 + SAR_UI_FIELD_HEIGHT) * 426 + 93], 0);

    /* A world that is twice the field: the integer scale is 2, every field
     * pixel becomes a 2x2 block, and the field fills the world. */
    sar_ui_viewport_present(field, world, 480, 320);
    CHECK_EQ(world[0], field[0]);
    CHECK_EQ(world[1], field[0]);
    CHECK_EQ(world[480], field[0]);
    CHECK_EQ(world[481], field[0]);
    CHECK_EQ(world[2], field[1]);
    CHECK_EQ(world[479], field[239]);
    CHECK_EQ(world[480 + 479], field[239]);

    /* A world smaller than the field: the fractional fallback. */
    sar_ui_viewport_present(field, world, 120, 80);
    CHECK_EQ(world[0], field[0]);
    CHECK_EQ(world[119], field[238]);
    CHECK_EQ(world[120], field[480]);
    CHECK_EQ(world[80], field[160]);
}

int main(void)
{
    TestMapGeometry();
    TestGbaLayout();
    TestWideLayout();
    TestScrollWrap();
    TestBoundedField();
    TestComposition();
    TestSecondBlock();
    TestPresentation();

    if (s_failures != 0) {
        printf("ui_viewport_test: %d failure(s)\n", s_failures);
        return 1;
    }

    printf("ui_viewport_test: all checks passed\n");
    return 0;
}
