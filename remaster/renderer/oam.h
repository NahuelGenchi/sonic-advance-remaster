/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Canonical OAM entry and the adapters for the two OAM views the upstream
 * ports write.
 *
 * The upstream decompilation has two mutually incompatible views of the same
 * 128-entry OAM table:
 *
 *   - the GBA-native 8-byte view (`OamData.all.attr0/1/2`), where the
 *     position lives inside the attribute words (Y in attr0 bits 0-7, X in
 *     attr1 bits 0-8);
 *   - the extended 12-byte view (`OamData.split.x/y`), used by the
 *     426x240 build, where X and Y are separate `s16` values and the
 *     attribute words are repacked into a single 32-bit bitfield.
 *
 * Both views are still written by game code in the same build: the Sonic
 * Advance 1 stage HUD writes the GBA-native view, while Sonic Advance 2 and
 * the widescreen sprite path write the extended view. A compile-time layout
 * switch cannot reconcile them, because the writers do not agree on it.
 *
 * This module defines one canonical entry and converts both views into it at
 * the engine/platform boundary. It is renderer-owned and depends on nothing
 * but the C standard library.
 *
 * Canonical entry
 * ---------------
 *   x, y          signed screen position (the authoritative coordinates)
 *   attr0         GBA attribute 0, bits 0-7 cleared (Y lives in `y`)
 *   attr1         GBA attribute 1, bits 0-8 cleared (X lives in `x`)
 *   attr2         GBA attribute 2, complete
 *   affine_param  the entry's affine parameter word, complete
 *
 * The coordinate bits are cleared in the stored attribute words because the
 * extended view can carry positions outside the GBA 9-bit X and 8-bit Y
 * ranges; keeping them in one place avoids two sources of truth. The
 * encoders put them back, so a canonical entry round-trips to either view.
 *
 * Writer-style detection
 * ----------------------
 * A 12-byte record does not carry a tag for the view it was written with,
 * but the two views are distinguishable:
 *
 *   - the extended `.split` view keeps 17 padding bits above its 15
 *     attribute bits and no writer ever sets them, while the `.all` view
 *     stores the GBA attribute 1 word in exactly that half of the 32-bit
 *     attribute word. A non-zero upper half therefore proves that the
 *     record was written through the `.all` view. This is the exact test,
 *     and it is the only one the conversion uses, so a record the extended
 *     view cannot draw never becomes a visible one;
 *   - the engine's unused entries are filled with the halfword pattern
 *     0x0200 and belong to the layout the engine writes.
 *
 * A `.all` record whose attribute 1 word is zero is the only ambiguous
 * case. The classification of such a record falls back to the prohibited
 * shape and object mode of the hardware, which only concerns records that
 * are not drawn at all. A `.all` record at X = 0 with an 8x8 shape and no
 * other attribute cannot be told apart from a `.split` record and is read
 * as `.split`.
 */

#ifndef SAR_RENDERER_OAM_H
#define SAR_RENDERER_OAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The upstream OAM table always has 128 entries. */
#define SAR_OAM_ENTRY_COUNT 128

/* Record sizes of the two upstream views. */
#define SAR_OAM_GBA_RECORD_SIZE 8
#define SAR_OAM_EXTENDED_RECORD_SIZE 12

/* GBA attribute 0. */
#define SAR_OAM_ATTR0_Y_MASK 0x00FFu
#define SAR_OAM_ATTR0_AFFINE_SHIFT 8
#define SAR_OAM_ATTR0_AFFINE_MASK 0x0300u
#define SAR_OAM_ATTR0_OBJ_SHIFT 10
#define SAR_OAM_ATTR0_OBJ_MASK 0x0C00u
#define SAR_OAM_ATTR0_MOSAIC 0x1000u
#define SAR_OAM_ATTR0_BPP 0x2000u
#define SAR_OAM_ATTR0_SHAPE_SHIFT 14
#define SAR_OAM_ATTR0_SHAPE_MASK 0xC000u

/* GBA attribute 1. */
#define SAR_OAM_ATTR1_X_MASK 0x01FFu
#define SAR_OAM_ATTR1_MATRIX_SHIFT 9
#define SAR_OAM_ATTR1_MATRIX_MASK 0x3E00u
#define SAR_OAM_ATTR1_HFLIP 0x1000u
#define SAR_OAM_ATTR1_VFLIP 0x2000u
#define SAR_OAM_ATTR1_SIZE_SHIFT 14
#define SAR_OAM_ATTR1_SIZE_MASK 0xC000u

/* GBA attribute 2. */
#define SAR_OAM_ATTR2_TILE_MASK 0x03FFu
#define SAR_OAM_ATTR2_PRIORITY_SHIFT 10
#define SAR_OAM_ATTR2_PRIORITY_MASK 0x0C00u
#define SAR_OAM_ATTR2_PALETTE_SHIFT 12
#define SAR_OAM_ATTR2_PALETTE_MASK 0xF000u

/* GBA attribute 0 bits 8-9. */
#define SAR_OAM_AFFINE_MODE_NORMAL 0
#define SAR_OAM_AFFINE_MODE_ROT_SCALE 1
#define SAR_OAM_AFFINE_MODE_DISABLED 2

/* Screen size used for the GBA 9-bit X and 8-bit Y wrap. With the 426x240
 * field the GBA view reaches X in [-85, 426] and Y in [-15, 240]; the
 * extended view has no such limit. */
typedef struct {
    int16_t width;
    int16_t height;
} SarOamLimits;

/* One canonical OAM entry. */
typedef struct {
    int16_t x;
    int16_t y;
    uint16_t attr0;
    uint16_t attr1;
    uint16_t attr2;
    uint16_t affine_param;
} SarOamEntry;

/* The writer style an OAM record was produced with. */
typedef enum {
    /* 8-byte GBA-native `OamData.all`. */
    SAR_OAM_VIEW_GBA_ALL = 0,
    /* 12-byte extended `OamData.all`: the GBA words inside a 12-byte record. */
    SAR_OAM_VIEW_EXTENDED_ALL = 1,
    /* 12-byte extended `OamData.split`: separate s16 X/Y and repacked words. */
    SAR_OAM_VIEW_EXTENDED_SPLIT = 2
} SarOamView;

/* The canonical 128-entry table plus the writer style of every entry. */
typedef struct {
    SarOamEntry entries[SAR_OAM_ENTRY_COUNT];
    SarOamView views[SAR_OAM_ENTRY_COUNT];
} SarOamTable;

/* Attribute accessors on the canonical entry. */
uint8_t sar_oam_affine_mode(const SarOamEntry *entry);
uint8_t sar_oam_obj_mode(const SarOamEntry *entry);
uint8_t sar_oam_shape(const SarOamEntry *entry);
uint8_t sar_oam_size(const SarOamEntry *entry);
uint8_t sar_oam_matrix(const SarOamEntry *entry);
uint8_t sar_oam_palette(const SarOamEntry *entry);
uint8_t sar_oam_priority(const SarOamEntry *entry);
uint16_t sar_oam_tile(const SarOamEntry *entry);
int sar_oam_mosaic(const SarOamEntry *entry);
int sar_oam_bpp(const SarOamEntry *entry);
int sar_oam_hflip(const SarOamEntry *entry);
int sar_oam_vflip(const SarOamEntry *entry);
int sar_oam_disabled(const SarOamEntry *entry);

/* Decoders: raw record bytes -> canonical entry. */
void sar_oam_decode_gba_all(SarOamEntry *entry, const uint8_t *raw, SarOamLimits limits);
void sar_oam_decode_extended_all(SarOamEntry *entry, const uint8_t *raw, SarOamLimits limits);
void sar_oam_decode_extended_split(SarOamEntry *entry, const uint8_t *raw);

/* Encoders: canonical entry -> raw record bytes. */
void sar_oam_encode_gba_all(uint8_t *raw, const SarOamEntry *entry);
void sar_oam_encode_extended_all(uint8_t *raw, const SarOamEntry *entry);
void sar_oam_encode_extended_split(uint8_t *raw, const SarOamEntry *entry);

/* Setters: one canonical entry from either writer style. */
void sar_oam_set_gba_all(SarOamEntry *entry, uint16_t attr0, uint16_t attr1, uint16_t attr2, uint16_t affine_param, SarOamLimits limits);
void sar_oam_set_extended_split(SarOamEntry *entry, int16_t x, int16_t y, uint32_t split_word, uint16_t attr2, uint16_t affine_param);
void sar_oam_set_attrs(SarOamEntry *entry, int16_t x, int16_t y, uint16_t attr0, uint16_t attr1, uint16_t attr2, uint16_t affine_param);

/* Writer-style detection for a 12-byte record. */
SarOamView sar_oam_classify_extended(const uint8_t *raw, SarOamLimits limits);

/* Exact detection of a GBA `.all` record: the GBA attribute 1 word sits in
 * the upper half of the 32-bit attribute word, where the extended `.split`
 * view keeps zero padding. */
int sar_oam_is_extended_all(const uint8_t *raw);

/* The canonical table. */
void sar_oam_table_clear(SarOamTable *table);
void sar_oam_table_decode(SarOamTable *table, const uint8_t *oam, size_t record_size, size_t count, const SarOamView *views, SarOamLimits limits);

/* Rewrite GBA `.all` records of a 12-byte OAM buffer into the extended
 * projection, in place, preserving the entry order and every record that is
 * not a GBA `.all` record. Returns the number of rewritten records. */
size_t sar_oam_rewrite_extended(uint8_t *oam, size_t count, const SarOamView *views, SarOamLimits limits);

#ifdef __cplusplus
}
#endif

#endif /* SAR_RENDERER_OAM_H */
