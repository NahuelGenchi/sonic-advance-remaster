/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The UI field composer, the bounded text-background map sampler and the
 * field presentation. See ui_viewport.h for what the module owns and why.
 *
 * The composer works in the coordinate space of the field: 240x160 pixels,
 * sampled from the engine's VRAM, palettes and canonical OAM table. It never
 * reads a rendered 426x240 frame, and it never samples a map row or column
 * outside the field.
 */

#include "ui_viewport.h"

#include <string.h>

/* The scene the frames are composed in. The screen/menu boundary sets it. */
static SarUiScene ui_scene = SAR_UI_SCENE_GAMEPLAY;

/* The sprite sizes, by shape and size bits. */
static const uint8_t obj_dim_table[3][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } }
};

/* The VRAM offsets the hardware uses. */
#define VRAM_SCREEN_BLOCK_SIZE 0x800u
#define VRAM_CHAR_BLOCK_SIZE  0x4000u
#define VRAM_OBJ_BASE          0x10000u
#define VRAM_OBJ_SIZE          0x8000u

/* The palette offsets. */
#define PALETTE_BG_BASE  0
#define PALETTE_OBJ_BASE 256

void sar_ui_viewport_set_scene(SarUiScene scene)
{
    ui_scene = scene;
}

SarUiScene sar_ui_viewport_get_scene(void)
{
    return ui_scene;
}

void sar_ui_map_geometry(uint16_t bgcnt, SarUiMapGeometry *geometry)
{
    uint32_t size = (uint32_t)(bgcnt >> 14) & 3u;

    geometry->width = (size & 1u) ? 512u : 256u;
    geometry->height = (size & 2u) ? 512u : 256u;
    geometry->blocks_x = geometry->width / 256u;
    geometry->blocks_y = geometry->height / 256u;
}

/*
 * The map entry of a point of the field.
 *
 * The GBA layout keeps a 512-wide map in two 32x32 tile blocks: the first
 * holds the left 256 pixels, the second the right 256 pixels, and the second
 * block is the next 2 KB block of the map region. The row stride inside a
 * block is 32 tiles.
 *
 * The widescreen layout of the port's level maps is one linear strip of 64
 * tiles per row, so the second 256-pixel block of a 512-wide map follows the
 * first one in the same row.
 */
static void map_sample(const SarUiMapGeometry *geometry, SarUiMapLayout layout, int hofs, int vofs, int x, int y, SarUiMapSample *sample)
{
    int32_t tile_x = (x + hofs) & (int32_t)(geometry->width - 1u);
    int32_t tile_y = (y + vofs) & (int32_t)(geometry->height - 1u);

    if (layout == SAR_UI_MAP_LAYOUT_WIDE) {
        int32_t band = (geometry->height == 512u) ? (int32_t)(geometry->width / 8u) * 32 : 0;

        sample->entry = band + ((tile_y & 255) >> 3) * (int32_t)(geometry->width / 8u) + (tile_x >> 3);
        sample->x = (uint32_t)tile_x;
        sample->y = (uint32_t)tile_y;
        return;
    }

    {
        int32_t block_x = tile_x >> 8;
        int32_t block_y = tile_y >> 8;
        int32_t row = (tile_y & 255) >> 3;
        int32_t col = (tile_x & 255) >> 3;

        sample->entry = (block_y * (int32_t)geometry->blocks_x + block_x) * 1024 + row * 32 + col;
        sample->x = (uint32_t)(tile_x & 255);
        sample->y = (uint32_t)(tile_y & 255);
    }
}

void sar_ui_map_sample(const SarUiMapGeometry *geometry, SarUiMapLayout layout, int hofs, int vofs, int x, int y, SarUiMapSample *sample)
{
    map_sample(geometry, layout, hofs, vofs, x, y, sample);
}

int32_t sar_ui_map_entry(const SarUiMapGeometry *geometry, SarUiMapLayout layout, int hofs, int vofs, int x, int y)
{
    SarUiMapSample sample;

    map_sample(geometry, layout, hofs, vofs, x, y, &sample);
    return sample.entry;
}

static uint16_t read_u16(const uint8_t *address)
{
    return (uint16_t)((uint16_t)address[0] | ((uint16_t)address[1] << 8));
}

static int bg_enabled(const SarUiViewportState *state, int bg)
{
    return (state->dispcnt & (uint16_t)(0x0100u << bg)) != 0;
}

static int obj_enabled(const SarUiViewportState *state)
{
    return (state->dispcnt & 0x1000u) != 0;
}

static int bg_is_affine(const SarUiViewportState *state, int bg)
{
    uint32_t mode = (uint32_t)(state->dispcnt & 7u);

    if (mode == 1u) {
        return bg == 2;
    }
    if (mode == 2u) {
        return bg == 2 || bg == 3;
    }
    return 0;
}

/* One text background scanline. */
static void render_text_bg(const SarUiViewportState *state, SarUiMapLayout layout, int bg, int y, uint16_t *row)
{
    uint16_t bgcnt = state->bgcnt[bg];
    SarUiMapGeometry geometry;
    const uint8_t *map_base;
    const uint8_t *tile_base;
    int is_8bpp = (bgcnt & 0x0080u) != 0;
    int hofs = state->bgofs_h[bg];
    int vofs = state->bgofs_v[bg];
    int x;

    sar_ui_map_geometry(bgcnt, &geometry);
    map_base = state->vram + VRAM_SCREEN_BLOCK_SIZE * (uint32_t)((bgcnt >> 8) & 0x1Fu);
    tile_base = state->vram + VRAM_CHAR_BLOCK_SIZE * (uint32_t)((bgcnt >> 2) & 3u);

    for (x = 0; x < SAR_UI_FIELD_WIDTH; x++) {
        SarUiMapSample sample;
        uint16_t tile;
        uint32_t tile_index;
        uint32_t tx;
        uint32_t ty;
        uint8_t colour;
        uint16_t entry;

        map_sample(&geometry, layout, hofs, vofs, x, y, &sample);
        tile = read_u16(map_base + (size_t)sample.entry * 2u);
        tile_index = (uint32_t)(tile & 0x03FFu);
        tx = sample.x & 7u;
        ty = sample.y & 7u;
        if (tile & 0x0400u) {
            tx = 7u - tx;
        }
        if (tile & 0x0800u) {
            ty = 7u - ty;
        }

        if (is_8bpp) {
            const uint8_t *tile_ptr = tile_base + tile_index * 64u + ty * 8u + tx;

            colour = *tile_ptr;
            if (colour == 0) {
                continue;
            }
            entry = state->palette[PALETTE_BG_BASE + colour];
        } else {
            const uint8_t *tile_ptr = tile_base + tile_index * 32u + ty * 4u + (tx >> 1);
            uint8_t packed = *tile_ptr;
            uint32_t sub_palette = (uint32_t)((tile >> 12) & 0x0Fu) * 16u;

            colour = (uint8_t)((packed >> (4u * (tx & 1u))) & 0x0Fu);
            if (colour == 0) {
                continue;
            }
            entry = state->palette[PALETTE_BG_BASE + sub_palette + colour];
        }

        row[x] = entry;
    }
}

/* One affine background scanline. */
static void render_affine_bg(const SarUiViewportState *state, int bg, int y, uint16_t *row)
{
    uint16_t bgcnt = state->bgcnt[bg];
    uint32_t map_size = (uint32_t)(bgcnt >> 14) & 3u;
    uint32_t width_height = 128u << map_size;
    uint32_t map_pitch = map_size + 4u;
    const uint8_t *map_base = state->vram + VRAM_SCREEN_BLOCK_SIZE * (uint32_t)((bgcnt >> 8) & 0x1Fu);
    const uint8_t *tile_base = state->vram + VRAM_CHAR_BLOCK_SIZE * (uint32_t)((bgcnt >> 2) & 3u);
    int wrap = (bgcnt & 0x2000u) != 0;
    int32_t reference_x;
    int32_t reference_y;
    int32_t pa;
    int32_t pc;
    int x;

    if (bg == 2) {
        reference_x = state->bg2_x + (int32_t)y * state->bg2_pb;
        reference_y = state->bg2_y + (int32_t)y * state->bg2_pd;
        pa = state->bg2_pa;
        pc = state->bg2_pc;
    } else {
        reference_x = state->bg3_x + (int32_t)y * state->bg3_pb;
        reference_y = state->bg3_y + (int32_t)y * state->bg3_pd;
        pa = state->bg3_pa;
        pc = state->bg3_pc;
    }

    for (x = 0; x < SAR_UI_FIELD_WIDTH; x++) {
        int32_t source_x = reference_x + (int32_t)x * pa;
        int32_t source_y = reference_y + (int32_t)x * pc;
        uint32_t pix_x;
        uint32_t pix_y;
        uint32_t map_offset;
        uint32_t tile_index;
        uint8_t colour;

        if (wrap) {
            pix_x = ((uint32_t)source_x >> 8) & (width_height - 1u);
            pix_y = ((uint32_t)source_y >> 8) & (width_height - 1u);
        } else {
            int32_t signed_x = source_x >> 8;
            int32_t signed_y = source_y >> 8;

            if (signed_x < 0 || signed_y < 0 || signed_x >= (int32_t)width_height || signed_y >= (int32_t)width_height) {
                continue;
            }
            pix_x = (uint32_t)signed_x;
            pix_y = (uint32_t)signed_y;
        }

        map_offset = (pix_x / 8u) + ((pix_y / 8u) << map_pitch);
        tile_index = map_base[map_offset];
        colour = tile_base[tile_index * 64u + (pix_x % 8u) + (pix_y % 8u) * 8u];
        if (colour == 0) {
            continue;
        }
        row[x] = state->palette[PALETTE_BG_BASE + colour];
    }
}

/* The sprites of one priority, back to front. */
static void render_sprites(const SarUiViewportState *state, int priority, int y, uint16_t *row)
{
    int one_dimensional = (state->dispcnt & 0x0040u) != 0;
    int index;

    if (state->oam == NULL) {
        return;
    }

    /* The hardware draws the objects of one priority back to front, and the
     * lower object number is in front. */
    for (index = SAR_OAM_ENTRY_COUNT - 1; index >= 0; index--) {
        const SarOamEntry *entry = &state->oam->entries[index];
        uint8_t shape;
        uint8_t size;
        int obj_width;
        int obj_height;
        int obj_x;
        int obj_y;
        int is_8bpp;
        int hflip;
        int vflip;
        uint32_t tile_number;
        uint32_t palette_bank;
        uint32_t tile_size;
        uint32_t tile_width;
        int row_in_object;
        int x;

        if (sar_oam_disabled(entry)) {
            continue;
        }
        if ((int)sar_oam_priority(entry) != priority) {
            continue;
        }
        /* Window objects and semi-transparent objects are not drawn here. */
        if (sar_oam_obj_mode(entry) != 0) {
            continue;
        }
        /* Affine sprites are not composed by this module. */
        if (sar_oam_affine_mode(entry) != SAR_OAM_AFFINE_MODE_NORMAL) {
            continue;
        }

        shape = sar_oam_shape(entry);
        size = sar_oam_size(entry);
        if (shape > 2u) {
            continue;
        }

        obj_width = obj_dim_table[shape][size][0];
        obj_height = obj_dim_table[shape][size][1];
        obj_x = entry->x;
        obj_y = entry->y;

        if (y < obj_y || y >= obj_y + obj_height) {
            continue;
        }
        if (obj_x >= SAR_UI_FIELD_WIDTH || obj_x + obj_width <= 0) {
            continue;
        }

        is_8bpp = sar_oam_bpp(entry);
        hflip = sar_oam_hflip(entry);
        vflip = sar_oam_vflip(entry);
        tile_number = sar_oam_tile(entry);
        palette_bank = sar_oam_palette(entry);
        tile_size = is_8bpp ? 64u : 32u;
        tile_width = is_8bpp ? 8u : 4u;

        row_in_object = y - obj_y;
        if (vflip) {
            row_in_object = obj_height - 1 - row_in_object;
        }

        for (x = obj_x > 0 ? obj_x : 0; x < obj_x + obj_width && x < SAR_UI_FIELD_WIDTH; x++) {
            int column = x - obj_x;
            int tile_column;
            int tile_row;
            uint32_t sprite_tile;
            uint32_t offset;
            const uint8_t *tile_ptr;
            uint8_t colour;

            if (hflip) {
                column = obj_width - 1 - column;
            }
            tile_column = column / 8;
            tile_row = row_in_object / 8;

            if (one_dimensional) {
                sprite_tile = (uint32_t)tile_row * (uint32_t)(obj_width / 8) + (uint32_t)tile_column;
            } else {
                sprite_tile = (uint32_t)tile_row * 32u + (uint32_t)tile_column;
            }

            offset = (tile_number + sprite_tile) * tile_size + (uint32_t)(row_in_object % 8) * tile_width;
            tile_ptr = state->vram + VRAM_OBJ_BASE + (offset & (VRAM_OBJ_SIZE - 1u));

            if (is_8bpp) {
                colour = tile_ptr[column & 7];
                if (colour == 0) {
                    continue;
                }
                row[x] = state->palette[PALETTE_OBJ_BASE + palette_bank * 256u + colour];
            } else {
                uint8_t packed = tile_ptr[(column & 7) >> 1];

                colour = (uint8_t)((packed >> (4u * (column & 1))) & 0x0Fu);
                if (colour == 0) {
                    continue;
                }
                row[x] = state->palette[PALETTE_OBJ_BASE + palette_bank * 16u + colour];
            }
        }
    }
}

void sar_ui_viewport_render_scanline(const SarUiViewportState *state, SarUiMapLayout layout, int y, uint16_t *row)
{
    int x;
    int priority;
    int bg;

    for (x = 0; x < SAR_UI_FIELD_WIDTH; x++) {
        row[x] = state->palette[PALETTE_BG_BASE];
    }

    for (priority = 3; priority >= 0; priority--) {
        for (bg = 3; bg >= 0; bg--) {
            if (!bg_enabled(state, bg)) {
                continue;
            }
            if ((int)(state->bgcnt[bg] & 3u) != priority) {
                continue;
            }
            if (bg_is_affine(state, bg)) {
                render_affine_bg(state, bg, y, row);
            } else {
                render_text_bg(state, layout, bg, y, row);
            }
        }
        if (obj_enabled(state)) {
            render_sprites(state, priority, y, row);
        }
    }
}

void sar_ui_viewport_render(const SarUiViewportState *state, SarUiMapLayout layout, uint16_t *field)
{
    int y;

    for (y = 0; y < SAR_UI_FIELD_HEIGHT; y++) {
        sar_ui_viewport_render_scanline(state, layout, y, field + (size_t)y * SAR_UI_FIELD_WIDTH);
    }
}

void sar_ui_viewport_present(const uint16_t *field, uint16_t *world, int world_width, int world_height)
{
    int scale = world_width / SAR_UI_FIELD_WIDTH;
    int scaled_height = world_height / SAR_UI_FIELD_HEIGHT;
    int destination_width;
    int destination_height;
    int offset_x;
    int offset_y;
    int x;
    int y;

    if (scale > scaled_height) {
        scale = scaled_height;
    }

    if (scale >= 1) {
        /* The integer scale: every field pixel becomes a square block. */
        destination_width = SAR_UI_FIELD_WIDTH * scale;
        destination_height = SAR_UI_FIELD_HEIGHT * scale;
    } else {
        /* The fractional fallback, for a frame smaller than the field. */
        double fit_x = (double)world_width / (double)SAR_UI_FIELD_WIDTH;
        double fit_y = (double)world_height / (double)SAR_UI_FIELD_HEIGHT;
        double fit = fit_x < fit_y ? fit_x : fit_y;

        destination_width = (int)((double)SAR_UI_FIELD_WIDTH * fit);
        destination_height = (int)((double)SAR_UI_FIELD_HEIGHT * fit);
        if (destination_width < 1) {
            destination_width = 1;
        }
        if (destination_height < 1) {
            destination_height = 1;
        }
    }

    offset_x = (world_width - destination_width) / 2;
    offset_y = (world_height - destination_height) / 2;

    memset(world, 0, (size_t)world_width * (size_t)world_height * sizeof(uint16_t));

    for (y = 0; y < destination_height; y++) {
        int source_y = (y * SAR_UI_FIELD_HEIGHT) / destination_height;

        for (x = 0; x < destination_width; x++) {
            int source_x = (x * SAR_UI_FIELD_WIDTH) / destination_width;

            world[(size_t)(offset_y + y) * (size_t)world_width + (size_t)(offset_x + x)] = field[(size_t)source_y * SAR_UI_FIELD_WIDTH + (size_t)source_x];
        }
    }
}
