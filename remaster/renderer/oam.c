/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Canonical OAM entry and the adapters for the two OAM views the upstream
 * ports write. See remaster/renderer/oam.h for the layout and the rules.
 *
 * Nothing here depends on the upstream headers: the module is renderer-owned
 * and only has to agree with the upstream record layouts, which are fixed by
 * the GBA hardware and by the upstream `OamData` union.
 */

#include "oam.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *raw)
{
    return (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
}

static uint32_t read_u32(const uint8_t *raw)
{
    return (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
}

static void write_u16(uint8_t *raw, uint16_t value)
{
    raw[0] = (uint8_t)(value & 0xFF);
    raw[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void write_u32(uint8_t *raw, uint32_t value)
{
    raw[0] = (uint8_t)(value & 0xFF);
    raw[1] = (uint8_t)((value >> 8) & 0xFF);
    raw[2] = (uint8_t)((value >> 16) & 0xFF);
    raw[3] = (uint8_t)((value >> 24) & 0xFF);
}

/*
 * The upstream 9-bit X and 8-bit Y wrap.
 *
 * The upstream software PPU reads the GBA-native view as
 *
 *     if (obj_x > DISPLAY_WIDTH)  obj_x -= 512;
 *     if (obj_y > DISPLAY_HEIGHT) obj_y -= 256;
 *
 * so a GBA `.all` writer can address the whole 9-bit X and 8-bit Y space.
 * The same rule is applied here, with the caller's screen size, so a
 * converted entry lands on the pixel the upstream native build would draw.
 */
static int16_t wrap_x(uint16_t attr1, SarOamLimits limits)
{
    int32_t x = (int32_t)(attr1 & SAR_OAM_ATTR1_X_MASK);

    if (x > limits.width) {
        x -= 512;
    }

    return (int16_t)x;
}

static int16_t wrap_y(uint16_t attr0, SarOamLimits limits)
{
    int32_t y = (int32_t)(attr0 & SAR_OAM_ATTR0_Y_MASK);

    if (y > limits.height) {
        y -= 256;
    }

    return (int16_t)y;
}

uint8_t sar_oam_affine_mode(const SarOamEntry *entry)
{
    return (uint8_t)((entry->attr0 & SAR_OAM_ATTR0_AFFINE_MASK) >> SAR_OAM_ATTR0_AFFINE_SHIFT);
}

uint8_t sar_oam_obj_mode(const SarOamEntry *entry)
{
    return (uint8_t)((entry->attr0 & SAR_OAM_ATTR0_OBJ_MASK) >> SAR_OAM_ATTR0_OBJ_SHIFT);
}

uint8_t sar_oam_shape(const SarOamEntry *entry)
{
    return (uint8_t)((entry->attr0 & SAR_OAM_ATTR0_SHAPE_MASK) >> SAR_OAM_ATTR0_SHAPE_SHIFT);
}

uint8_t sar_oam_size(const SarOamEntry *entry)
{
    return (uint8_t)((entry->attr1 & SAR_OAM_ATTR1_SIZE_MASK) >> SAR_OAM_ATTR1_SIZE_SHIFT);
}

uint8_t sar_oam_matrix(const SarOamEntry *entry)
{
    return (uint8_t)((entry->attr1 & SAR_OAM_ATTR1_MATRIX_MASK) >> SAR_OAM_ATTR1_MATRIX_SHIFT);
}

uint8_t sar_oam_palette(const SarOamEntry *entry)
{
    return (uint8_t)((entry->attr2 & SAR_OAM_ATTR2_PALETTE_MASK) >> SAR_OAM_ATTR2_PALETTE_SHIFT);
}

uint8_t sar_oam_priority(const SarOamEntry *entry)
{
    return (uint8_t)((entry->attr2 & SAR_OAM_ATTR2_PRIORITY_MASK) >> SAR_OAM_ATTR2_PRIORITY_SHIFT);
}

uint16_t sar_oam_tile(const SarOamEntry *entry)
{
    return (uint16_t)(entry->attr2 & SAR_OAM_ATTR2_TILE_MASK);
}

int sar_oam_mosaic(const SarOamEntry *entry)
{
    return (entry->attr0 & SAR_OAM_ATTR0_MOSAIC) != 0;
}

int sar_oam_bpp(const SarOamEntry *entry)
{
    return (entry->attr0 & SAR_OAM_ATTR0_BPP) != 0;
}

int sar_oam_hflip(const SarOamEntry *entry)
{
    return (entry->attr1 & SAR_OAM_ATTR1_HFLIP) != 0;
}

int sar_oam_vflip(const SarOamEntry *entry)
{
    return (entry->attr1 & SAR_OAM_ATTR1_VFLIP) != 0;
}

int sar_oam_disabled(const SarOamEntry *entry)
{
    return sar_oam_affine_mode(entry) == SAR_OAM_AFFINE_MODE_DISABLED;
}

void sar_oam_decode_gba_all(SarOamEntry *entry, const uint8_t *raw, SarOamLimits limits)
{
    uint16_t attr0 = read_u16(raw + 0);
    uint16_t attr1 = read_u16(raw + 2);

    entry->x = wrap_x(attr1, limits);
    entry->y = wrap_y(attr0, limits);
    entry->attr0 = (uint16_t)(attr0 & (uint16_t)~SAR_OAM_ATTR0_Y_MASK);
    entry->attr1 = (uint16_t)(attr1 & (uint16_t)~SAR_OAM_ATTR1_X_MASK);
    entry->attr2 = read_u16(raw + 4);
    entry->affine_param = read_u16(raw + 6);
}

void sar_oam_decode_extended_all(SarOamEntry *entry, const uint8_t *raw, SarOamLimits limits)
{
    /*
     * The extended union's `.all` view puts the GBA words at offsets 4, 6 and
     * 8. Its own `s16` X and Y at offsets 0 and 2 belong to the `.split`
     * view and are not written by `.all` writers, so the position is taken
     * from the GBA words.
     */
    uint16_t attr0 = read_u16(raw + 4);
    uint16_t attr1 = read_u16(raw + 6);

    entry->x = wrap_x(attr1, limits);
    entry->y = wrap_y(attr0, limits);
    entry->attr0 = (uint16_t)(attr0 & (uint16_t)~SAR_OAM_ATTR0_Y_MASK);
    entry->attr1 = (uint16_t)(attr1 & (uint16_t)~SAR_OAM_ATTR1_X_MASK);
    entry->attr2 = read_u16(raw + 8);
    entry->affine_param = read_u16(raw + 10);
}

void sar_oam_decode_extended_split(SarOamEntry *entry, const uint8_t *raw)
{
    uint32_t word = read_u32(raw + 4);

    entry->x = (int16_t)read_u16(raw + 0);
    entry->y = (int16_t)read_u16(raw + 2);
    entry->attr0 = (uint16_t)((word & 0xFFu) << 8);
    entry->attr1 = (uint16_t)(((word >> 8) & 0x7Fu) << 9);
    entry->attr2 = read_u16(raw + 8);
    entry->affine_param = read_u16(raw + 10);
}

void sar_oam_encode_gba_all(uint8_t *raw, const SarOamEntry *entry)
{
    write_u16(raw + 0, (uint16_t)(entry->attr0 | (uint16_t)(entry->y & SAR_OAM_ATTR0_Y_MASK)));
    write_u16(raw + 2, (uint16_t)(entry->attr1 | (uint16_t)(entry->x & SAR_OAM_ATTR1_X_MASK)));
    write_u16(raw + 4, entry->attr2);
    write_u16(raw + 6, entry->affine_param);
}

void sar_oam_encode_extended_all(uint8_t *raw, const SarOamEntry *entry)
{
    write_u16(raw + 0, (uint16_t)entry->x);
    write_u16(raw + 2, (uint16_t)entry->y);
    write_u16(raw + 4, (uint16_t)(entry->attr0 | (uint16_t)(entry->y & SAR_OAM_ATTR0_Y_MASK)));
    write_u16(raw + 6, (uint16_t)(entry->attr1 | (uint16_t)(entry->x & SAR_OAM_ATTR1_X_MASK)));
    write_u16(raw + 8, entry->attr2);
    write_u16(raw + 10, entry->affine_param);
}

void sar_oam_encode_extended_split(uint8_t *raw, const SarOamEntry *entry)
{
    /*
     * The extended view repacks the attribute words: attribute 0's high
     * byte takes word bits 0-7 and attribute 1's bits 9-15 take word bits
     * 8-14. The remaining word bits are the upstream padding and stay zero.
     */
    uint32_t word = (uint32_t)((entry->attr0 >> 8) & 0xFFu) | (uint32_t)((entry->attr1 >> 1) & 0x7F00u);

    write_u16(raw + 0, (uint16_t)entry->x);
    write_u16(raw + 2, (uint16_t)entry->y);
    write_u32(raw + 4, word);
    write_u16(raw + 8, entry->attr2);
    write_u16(raw + 10, entry->affine_param);
}

void sar_oam_set_gba_all(SarOamEntry *entry, uint16_t attr0, uint16_t attr1, uint16_t attr2, uint16_t affine_param, SarOamLimits limits)
{
    entry->x = wrap_x(attr1, limits);
    entry->y = wrap_y(attr0, limits);
    entry->attr0 = (uint16_t)(attr0 & (uint16_t)~SAR_OAM_ATTR0_Y_MASK);
    entry->attr1 = (uint16_t)(attr1 & (uint16_t)~SAR_OAM_ATTR1_X_MASK);
    entry->attr2 = attr2;
    entry->affine_param = affine_param;
}

void sar_oam_set_extended_split(SarOamEntry *entry, int16_t x, int16_t y, uint32_t split_word, uint16_t attr2, uint16_t affine_param)
{
    entry->x = x;
    entry->y = y;
    entry->attr0 = (uint16_t)((split_word & 0xFFu) << 8);
    entry->attr1 = (uint16_t)(((split_word >> 8) & 0x7Fu) << 9);
    entry->attr2 = attr2;
    entry->affine_param = affine_param;
}

void sar_oam_set_attrs(SarOamEntry *entry, int16_t x, int16_t y, uint16_t attr0, uint16_t attr1, uint16_t attr2, uint16_t affine_param)
{
    entry->x = x;
    entry->y = y;
    entry->attr0 = (uint16_t)(attr0 & (uint16_t)~SAR_OAM_ATTR0_Y_MASK);
    entry->attr1 = (uint16_t)(attr1 & (uint16_t)~SAR_OAM_ATTR1_X_MASK);
    entry->attr2 = attr2;
    entry->affine_param = affine_param;
}

/*
 * The engine fills its unused OAM entries with the halfword pattern 0x0200
 * (`DmaFill16(3, 0x200, ...)`). The pattern is the same in both views, but
 * it belongs to the layout the engine writes, and in the extended view it
 * reads as an off-screen sprite.
 */
static int is_unused_pattern(const uint8_t *raw)
{
    int i;

    for (i = 0; i < SAR_OAM_EXTENDED_RECORD_SIZE; i += 2) {
        if (raw[i] != 0x00 || raw[i + 1] != 0x02) {
            return 0;
        }
    }

    return 1;
}

int sar_oam_is_extended_all(const uint8_t *raw)
{
    /*
     * The extended `.split` view keeps 17 padding bits above the 15
     * attribute bits, and no `.split` writer ever sets them. The `.all`
     * view stores the GBA attribute 1 word in exactly that half of the
     * 32-bit word. A non-zero upper half therefore proves that the record
     * was written through the GBA `.all` view.
     */
    return ((read_u32(raw + 4) >> 16) & 0xFFFFu) != 0;
}

SarOamView sar_oam_classify_extended(const uint8_t *raw, SarOamLimits limits)
{
    uint32_t word = read_u32(raw + 4);
    uint16_t attr0 = (uint16_t)(word & 0xFFFFu);
    uint8_t split_shape = (uint8_t)((word >> 6) & 3u);
    uint8_t split_obj_mode = (uint8_t)((word >> 2) & 3u);
    uint8_t split_affine_mode = (uint8_t)(word & 3u);
    uint8_t all_shape = (uint8_t)((attr0 >> 14) & 3u);
    uint8_t all_obj_mode = (uint8_t)((attr0 >> 10) & 3u);
    uint8_t all_affine_mode = (uint8_t)((attr0 >> 8) & 3u);

    if (is_unused_pattern(raw)) {
        return SAR_OAM_VIEW_EXTENDED_SPLIT;
    }

    if (sar_oam_is_extended_all(raw)) {
        return SAR_OAM_VIEW_EXTENDED_ALL;
    }

    /*
     * Attribute 1 is zero, so both views are possible. A prohibited shape
     * and a prohibited object mode are never drawn, so a record the
     * extended view cannot draw is read with the GBA view. The reading only
     * matters for records that are not drawn at all.
     */
    if (split_shape == 3 || split_obj_mode == 3 || split_affine_mode == 3) {
        if (all_shape != 3 && all_obj_mode != 3 && all_affine_mode != 3) {
            return SAR_OAM_VIEW_EXTENDED_ALL;
        }
    }

    (void)limits;
    return SAR_OAM_VIEW_EXTENDED_SPLIT;
}

void sar_oam_table_clear(SarOamTable *table)
{
    int i;

    for (i = 0; i < SAR_OAM_ENTRY_COUNT; i++) {
        memset(&table->entries[i], 0, sizeof(table->entries[i]));
        table->views[i] = SAR_OAM_VIEW_EXTENDED_SPLIT;
    }
}

void sar_oam_table_decode(SarOamTable *table, const uint8_t *oam, size_t record_size, size_t count, const SarOamView *views, SarOamLimits limits)
{
    size_t i;

    sar_oam_table_clear(table);

    if (count > SAR_OAM_ENTRY_COUNT) {
        count = SAR_OAM_ENTRY_COUNT;
    }

    for (i = 0; i < count; i++) {
        const uint8_t *raw = oam + i * record_size;
        SarOamView view;

        if (record_size == SAR_OAM_GBA_RECORD_SIZE) {
            view = SAR_OAM_VIEW_GBA_ALL;
        } else if (views != NULL) {
            view = views[i];
        } else {
            view = sar_oam_classify_extended(raw, limits);
        }

        table->views[i] = view;

        switch (view) {
            case SAR_OAM_VIEW_GBA_ALL:
                sar_oam_decode_gba_all(&table->entries[i], raw, limits);
                break;
            case SAR_OAM_VIEW_EXTENDED_ALL:
                sar_oam_decode_extended_all(&table->entries[i], raw, limits);
                break;
            case SAR_OAM_VIEW_EXTENDED_SPLIT:
            default:
                sar_oam_decode_extended_split(&table->entries[i], raw);
                break;
        }
    }
}

size_t sar_oam_rewrite_extended(uint8_t *oam, size_t count, const SarOamView *views, SarOamLimits limits)
{
    size_t i;
    size_t rewritten = 0;

    if (count > SAR_OAM_ENTRY_COUNT) {
        count = SAR_OAM_ENTRY_COUNT;
    }

    for (i = 0; i < count; i++) {
        uint8_t *raw = oam + i * SAR_OAM_EXTENDED_RECORD_SIZE;
        SarOamEntry entry;
        int is_all;

        if (views != NULL) {
            is_all = views[i] == SAR_OAM_VIEW_GBA_ALL || views[i] == SAR_OAM_VIEW_EXTENDED_ALL;
        } else {
            /*
             * Only the exact detection converts a record. The ambiguous
             * case of the writer-style detection is left alone, so a record
             * that the extended view cannot draw never becomes a visible
             * one.
             */
            is_all = sar_oam_is_extended_all(raw) && !is_unused_pattern(raw);
        }

        if (!is_all) {
            continue;
        }

        sar_oam_decode_extended_all(&entry, raw, limits);

        /*
         * A hidden entry is not drawn in either view, so it is left byte for
         * byte as the engine wrote it. That keeps the buffer identical for
         * the unused entries the engine fills with its disable pattern.
         */
        if (sar_oam_disabled(&entry)) {
            continue;
        }

        sar_oam_encode_extended_split(raw, &entry);
        rewritten++;
    }

    return rewritten;
}
