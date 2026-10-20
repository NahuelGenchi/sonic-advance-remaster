/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Deterministic tests for the canonical OAM module.
 *
 * The test uses synthetic entries only. It covers a normal sprite, an affine
 * sprite, a hidden sprite, horizontal and vertical flips, mosaic, priority,
 * shape and size, negative and wrapped coordinates, the 128-entry table and
 * the writer-style classification. It also proves that the GBA-native `.all`
 * writer and the extended `.split` writer produce the same canonical entry
 * for the same sprite.
 *
 * Build and run it with tools/oam_test.sh.
 */

#include <stdio.h>
#include <string.h>

#include "oam.h"

static int failures;
static int checks;

#define CHECK(condition)                                                        \
    do {                                                                         \
        checks++;                                                                \
        if (!(condition)) {                                                      \
            failures++;                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
        }                                                                        \
    } while (0)

#define CHECK_EQ_U16(actual, expected)                                          \
    do {                                                                         \
        unsigned int a_ = (unsigned int)(actual);                                \
        unsigned int e_ = (unsigned int)(expected);                              \
        checks++;                                                                \
        if (a_ != e_) {                                                          \
            failures++;                                                          \
            printf("FAIL %s:%d: %s == 0x%04X, expected 0x%04X\n", __FILE__,      \
                   __LINE__, #actual, a_, e_);                                   \
        }                                                                        \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                          \
    do {                                                                         \
        int a_ = (int)(actual);                                                  \
        int e_ = (int)(expected);                                                \
        checks++;                                                                \
        if (a_ != e_) {                                                          \
            failures++;                                                          \
            printf("FAIL %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__,     \
                   #actual, a_, e_);                                             \
        }                                                                        \
    } while (0)

static const SarOamLimits widescreen = {426, 240};

static void put_u16(uint8_t *raw, int offset, unsigned int value)
{
    raw[offset] = (uint8_t)(value & 0xFF);
    raw[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

static void put_u32(uint8_t *raw, int offset, unsigned int value)
{
    raw[offset] = (uint8_t)(value & 0xFF);
    raw[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    raw[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    raw[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static unsigned int get_u16(const uint8_t *raw, int offset)
{
    return (unsigned int)raw[offset] | ((unsigned int)raw[offset + 1] << 8);
}

/* Build the 8-byte GBA-native record of a sprite. */
static void build_gba_all(uint8_t *raw, unsigned int attr0, unsigned int attr1, unsigned int attr2, unsigned int affine_param)
{
    put_u16(raw, 0, attr0);
    put_u16(raw, 2, attr1);
    put_u16(raw, 4, attr2);
    put_u16(raw, 6, affine_param);
}

/* Build the 12-byte extended `.split` record of a sprite. */
static void build_extended_split(uint8_t *raw, int x, int y, unsigned int attr0, unsigned int attr1, unsigned int attr2,
                                 unsigned int affine_param)
{
    unsigned int word = ((attr0 >> 8) & 0xFFu) | ((attr1 >> 1) & 0x7F00u);

    put_u16(raw, 0, (unsigned int)x & 0xFFFFu);
    put_u16(raw, 2, (unsigned int)y & 0xFFFFu);
    put_u32(raw, 4, word);
    put_u16(raw, 8, attr2);
    put_u16(raw, 10, affine_param);
}

/* Build the 12-byte extended `.all` record of a sprite, the way the Sonic
 * Advance 1 stage HUD writes it: the GBA words at offsets 4, 6 and 8, and
 * stale coordinates at offsets 0 and 2. */
static void build_extended_all(uint8_t *raw, unsigned int stale_x, unsigned int stale_y, unsigned int attr0, unsigned int attr1,
                               unsigned int attr2, unsigned int affine_param)
{
    put_u16(raw, 0, stale_x);
    put_u16(raw, 2, stale_y);
    put_u16(raw, 4, attr0);
    put_u16(raw, 6, attr1);
    put_u16(raw, 8, attr2);
    put_u16(raw, 10, affine_param);
}

static int entries_equal(const SarOamEntry *a, const SarOamEntry *b)
{
    return a->x == b->x && a->y == b->y && a->attr0 == b->attr0 && a->attr1 == b->attr1 && a->attr2 == b->attr2
           && a->affine_param == b->affine_param;
}

/* A normal 16-colour square sprite at (100, 40). */
static void test_normal_sprite(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry from_gba;
    SarOamEntry from_split;

    /* attr0: Y in bits 0-7, shape 0 (square). attr1: X in bits 0-8, size 2. */
    build_gba_all(gba, 0x0028u, 0x8064u, 0x3523u, 0x0000u);
    build_extended_split(split, 100, 40, 0x0028u, 0x8064u, 0x3523u, 0x0000u);

    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    sar_oam_decode_extended_split(&from_split, split);

    CHECK(entries_equal(&from_gba, &from_split));
    CHECK_EQ_INT(from_gba.x, 100);
    CHECK_EQ_INT(from_gba.y, 40);
    CHECK_EQ_U16(from_gba.attr0, 0x0000u);
    CHECK_EQ_U16(from_gba.attr1, 0x8000u);
    CHECK_EQ_U16(from_gba.attr2, 0x3523u);
    CHECK_EQ_INT(sar_oam_shape(&from_gba), 0);
    CHECK_EQ_INT(sar_oam_size(&from_gba), 2);
    CHECK_EQ_INT(sar_oam_tile(&from_gba), 0x123);
    CHECK_EQ_INT(sar_oam_priority(&from_gba), 1);
    CHECK_EQ_INT(sar_oam_palette(&from_gba), 3);
    CHECK_EQ_INT(sar_oam_affine_mode(&from_gba), SAR_OAM_AFFINE_MODE_NORMAL);
    CHECK_EQ_INT(sar_oam_disabled(&from_gba), 0);
}

/* An affine sprite keeps its matrix number and its affine parameter word. */
static void test_affine_sprite(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry from_gba;
    SarOamEntry from_split;
    unsigned int attr0 = 0x0140u | (2u << 14); /* Y=64, affine mode 1, shape 2 */
    unsigned int attr1 = 0x0064u | (5u << 9) | (1u << 14); /* X=100, matrix 5, size 1 */

    build_gba_all(gba, attr0, attr1, 0x0100u, 0x8123u);
    build_extended_split(split, 100, 64, attr0, attr1, 0x0100u, 0x8123u);

    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    sar_oam_decode_extended_split(&from_split, split);

    CHECK(entries_equal(&from_gba, &from_split));
    CHECK_EQ_INT(sar_oam_affine_mode(&from_gba), SAR_OAM_AFFINE_MODE_ROT_SCALE);
    CHECK_EQ_INT(sar_oam_matrix(&from_gba), 5);
    CHECK_EQ_U16(from_gba.affine_param, 0x8123u);
    CHECK_EQ_INT(sar_oam_shape(&from_gba), 2);
    CHECK_EQ_INT(sar_oam_size(&from_gba), 1);
}

/* A hidden sprite stays hidden. */
static void test_hidden_sprite(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    SarOamEntry entry;

    build_gba_all(gba, 0x0200u | 0x0010u, 0x0020u, 0x0000u, 0x0200u);
    sar_oam_decode_gba_all(&entry, gba, widescreen);

    CHECK_EQ_INT(sar_oam_affine_mode(&entry), SAR_OAM_AFFINE_MODE_DISABLED);
    CHECK_EQ_INT(sar_oam_disabled(&entry), 1);
}

/* Flips live in attribute 1 bits 12 and 13, which are the extended view's
 * matrix bits 3 and 4. */
static void test_flips(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry from_gba;
    SarOamEntry from_split;
    unsigned int attr0 = 0x0010u;
    unsigned int attr1 = 0x0020u | SAR_OAM_ATTR1_HFLIP | SAR_OAM_ATTR1_VFLIP;

    build_gba_all(gba, attr0, attr1, 0x0000u, 0x0000u);
    build_extended_split(split, 32, 16, attr0, attr1, 0x0000u, 0x0000u);

    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    sar_oam_decode_extended_split(&from_split, split);

    CHECK(entries_equal(&from_gba, &from_split));
    CHECK_EQ_INT(sar_oam_hflip(&from_gba), 1);
    CHECK_EQ_INT(sar_oam_vflip(&from_gba), 1);
    /* The extended view keeps the flips in the matrix field. */
    CHECK_EQ_INT(sar_oam_matrix(&from_split), 0x18);
    CHECK_EQ_INT(sar_oam_hflip(&from_split), 1);
    CHECK_EQ_INT(sar_oam_vflip(&from_split), 1);
}

/* Mosaic and the 256-colour bit survive the conversion. */
static void test_mosaic_and_bpp(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry from_gba;
    SarOamEntry from_split;
    unsigned int attr0 = 0x0010u | SAR_OAM_ATTR0_MOSAIC | SAR_OAM_ATTR0_BPP;

    build_gba_all(gba, attr0, 0x0020u, 0x0000u, 0x0000u);
    build_extended_split(split, 32, 16, attr0, 0x0020u, 0x0000u, 0x0000u);

    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    sar_oam_decode_extended_split(&from_split, split);

    CHECK(entries_equal(&from_gba, &from_split));
    CHECK_EQ_INT(sar_oam_mosaic(&from_gba), 1);
    CHECK_EQ_INT(sar_oam_bpp(&from_gba), 1);
    CHECK_EQ_INT(sar_oam_mosaic(&from_split), 1);
    CHECK_EQ_INT(sar_oam_bpp(&from_split), 1);
}

/* Priority, palette and tile number survive the conversion. */
static void test_priority_palette_tile(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry from_gba;
    SarOamEntry from_split;

    build_gba_all(gba, 0x0010u, 0x0020u, 0x03FFu | (3u << 10) | (7u << 12), 0x0000u);
    build_extended_split(split, 32, 16, 0x0010u, 0x0020u, 0x03FFu | (3u << 10) | (7u << 12), 0x0000u);

    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    sar_oam_decode_extended_split(&from_split, split);

    CHECK(entries_equal(&from_gba, &from_split));
    CHECK_EQ_INT(sar_oam_priority(&from_gba), 3);
    CHECK_EQ_INT(sar_oam_palette(&from_gba), 7);
    CHECK_EQ_INT(sar_oam_tile(&from_gba), 0x3FF);
}

/* Shape and size survive the conversion. */
static void test_shape_and_size(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry from_gba;
    SarOamEntry from_split;
    unsigned int attr0 = 0x0010u | (1u << 14); /* horizontal */
    unsigned int attr1 = 0x0020u | (3u << 14);  /* size 3 */

    build_gba_all(gba, attr0, attr1, 0x0000u, 0x0000u);
    build_extended_split(split, 32, 16, attr0, attr1, 0x0000u, 0x0000u);

    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    sar_oam_decode_extended_split(&from_split, split);

    CHECK(entries_equal(&from_gba, &from_split));
    CHECK_EQ_INT(sar_oam_shape(&from_gba), 1);
    CHECK_EQ_INT(sar_oam_size(&from_gba), 3);
}

/* The GBA 9-bit X and 8-bit Y wrap exactly like the upstream software PPU,
 * and the extended view keeps its own signed range. */
static void test_negative_and_wrapped_coordinates(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry from_gba;
    SarOamEntry from_split;

    /* X = 500 and Y = 253 in the GBA view wrap to -12 and -3. */
    build_gba_all(gba, 0x00FDu, 0x01F4u, 0x0000u, 0x0000u);
    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    CHECK_EQ_INT(from_gba.x, -12);
    CHECK_EQ_INT(from_gba.y, -3);

    /* The extended view writes the same sprite with signed coordinates. */
    build_extended_split(split, -12, -3, 0x00FDu, 0x01F4u, 0x0000u, 0x0000u);
    sar_oam_decode_extended_split(&from_split, split);
    CHECK(entries_equal(&from_gba, &from_split));
    CHECK_EQ_INT(from_split.x, -12);
    CHECK_EQ_INT(from_split.y, -3);

    /* The extended view also carries positions the GBA view cannot hold. */
    build_extended_split(split, 500, 300, 0x0010u, 0x0020u, 0x0000u, 0x0000u);
    sar_oam_decode_extended_split(&from_split, split);
    CHECK_EQ_INT(from_split.x, 500);
    CHECK_EQ_INT(from_split.y, 300);

    /* A coordinate inside the screen is not wrapped. */
    build_gba_all(gba, 0x0028u, 0x0064u, 0x0000u, 0x0000u);
    sar_oam_decode_gba_all(&from_gba, gba, widescreen);
    CHECK_EQ_INT(from_gba.x, 100);
    CHECK_EQ_INT(from_gba.y, 40);
}

/* A canonical entry round-trips to both views. */
static void test_round_trip(void)
{
    uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    uint8_t gba_again[SAR_OAM_GBA_RECORD_SIZE];
    uint8_t split_again[SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamEntry entry;

    build_gba_all(gba, 0x8140u, 0x8264u, 0x3523u, 0x8123u);
    sar_oam_decode_gba_all(&entry, gba, widescreen);
    sar_oam_encode_gba_all(gba_again, &entry);
    CHECK_EQ_INT(memcmp(gba, gba_again, sizeof(gba)), 0);

    build_extended_split(split, 100, 64, 0x8140u, 0x8264u, 0x3523u, 0x8123u);
    sar_oam_decode_extended_split(&entry, split);
    sar_oam_encode_extended_split(split_again, &entry);
    CHECK_EQ_INT(memcmp(split, split_again, sizeof(split)), 0);

    /* The 12-byte `.all` record round-trips through the canonical entry. */
    build_extended_all(split, 0x006Cu, 0x0010u, 0x8140u, 0x8264u, 0x3523u, 0x8123u);
    sar_oam_decode_extended_all(&entry, split, widescreen);
    sar_oam_encode_extended_all(split_again, &entry);
    CHECK_EQ_INT(get_u16(split_again, 4), 0x8140u);
    CHECK_EQ_INT(get_u16(split_again, 6), 0x8264u);
    CHECK_EQ_INT(get_u16(split_again, 8), 0x3523u);
    CHECK_EQ_INT(get_u16(split_again, 10), 0x8123u);
}

/* Both writer styles produce the same canonical entry for the same sprite. */
static void test_writer_styles_agree(void)
{
    static const struct {
        int x;
        int y;
        unsigned int attr0;
        unsigned int attr1;
        unsigned int attr2;
        unsigned int affine_param;
    } sprites[] = {
        {0, 0, 0x0000u, 0x0000u, 0x0000u, 0x0000u},
        {100, 40, 0x0000u, 0x8000u, 0x3523u, 0x0000u},
        {200, 120, 0x0100u, 0x0A00u, 0x0123u, 0x1111u},
        {7, 3, 0x1000u, 0x3000u, 0xC000u, 0x2222u},
        {255, 239, 0x2000u, 0x4000u, 0x0000u, 0x3333u},
        {426, 240, 0xC000u, 0xC000u, 0xFFFFu, 0x4444u},
        {-12, -3, 0x0000u, 0x0000u, 0x0000u, 0x0000u},
        /* The GBA view reaches X = -85 and Y = -15; Y = 240 is the bottom
         * edge of the 426x240 field and is not wrapped. */
        {-85, -15, 0x1400u, 0x1200u, 0x0ABCu, 0x5555u},
    };
    size_t i;

    for (i = 0; i < sizeof(sprites) / sizeof(sprites[0]); i++) {
        uint8_t gba[SAR_OAM_GBA_RECORD_SIZE];
        uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
        uint8_t extended_all[SAR_OAM_EXTENDED_RECORD_SIZE];
        SarOamEntry from_gba;
        SarOamEntry from_split;
        SarOamEntry from_extended_all;
        unsigned int attr0 = (sprites[i].attr0 & ~SAR_OAM_ATTR0_Y_MASK) | (unsigned int)(sprites[i].y & 0xFF);
        unsigned int attr1 = (sprites[i].attr1 & ~SAR_OAM_ATTR1_X_MASK) | (unsigned int)(sprites[i].x & 0x1FF);

        build_gba_all(gba, attr0, attr1, sprites[i].attr2, sprites[i].affine_param);
        build_extended_split(split, sprites[i].x, sprites[i].y, sprites[i].attr0, sprites[i].attr1, sprites[i].attr2,
                              sprites[i].affine_param);
        build_extended_all(extended_all, 0x006Cu, 0x0010u, attr0, attr1, sprites[i].attr2, sprites[i].affine_param);

        sar_oam_decode_gba_all(&from_gba, gba, widescreen);
        sar_oam_decode_extended_split(&from_split, split);
        sar_oam_decode_extended_all(&from_extended_all, extended_all, widescreen);

        if (!entries_equal(&from_gba, &from_split)) {
            failures++;
            printf("FAIL sprite %u: .all and .split differ\n", (unsigned int)i);
        }
        if (!entries_equal(&from_gba, &from_extended_all)) {
            failures++;
            printf("FAIL sprite %u: .all and extended .all differ\n", (unsigned int)i);
        }
        checks += 2;
    }
}

/* The writer style of a 12-byte record is detected from the record itself. */
static void test_classification(void)
{
    uint8_t split[SAR_OAM_EXTENDED_RECORD_SIZE];
    uint8_t all[SAR_OAM_EXTENDED_RECORD_SIZE];
    uint8_t fill[SAR_OAM_EXTENDED_RECORD_SIZE];

    build_extended_split(split, 100, 40, 0x0000u, 0x8000u, 0x3523u, 0x0000u);
    CHECK_EQ_INT(sar_oam_classify_extended(split, widescreen), SAR_OAM_VIEW_EXTENDED_SPLIT);

    build_extended_all(all, 0x006Cu, 0x0010u, 0x0028u, 0x8064u, 0x3523u, 0x0000u);
    CHECK_EQ_INT(sar_oam_classify_extended(all, widescreen), SAR_OAM_VIEW_EXTENDED_ALL);
    CHECK_EQ_INT(sar_oam_is_extended_all(all), 1);

    /* An ambiguous record: attribute 1 is zero, so both views are possible
     * and the extended view is used. */
    build_extended_all(all, 0x006Cu, 0x0010u, 0x0000u, 0x0000u, 0x3523u, 0x0000u);
    CHECK_EQ_INT(sar_oam_classify_extended(all, widescreen), SAR_OAM_VIEW_EXTENDED_SPLIT);
    CHECK_EQ_INT(sar_oam_is_extended_all(all), 0);

    /* A record the extended view cannot draw is read with the GBA view. */
    build_extended_split(all, 10, 20, 0xC000u, 0x0000u, 0x3523u, 0x0000u);
    CHECK_EQ_INT(sar_oam_classify_extended(all, widescreen), SAR_OAM_VIEW_EXTENDED_ALL);
    CHECK_EQ_INT(sar_oam_is_extended_all(all), 0);

    /* The engine's unused-entry pattern is a hidden GBA entry. */
    memset(fill, 0, sizeof(fill));
    put_u16(fill, 0, 0x0200u);
    put_u16(fill, 2, 0x0200u);
    put_u16(fill, 4, 0x0200u);
    put_u16(fill, 6, 0x0200u);
    put_u16(fill, 8, 0x0200u);
    put_u16(fill, 10, 0x0200u);
    CHECK_EQ_INT(sar_oam_classify_extended(fill, widescreen), SAR_OAM_VIEW_EXTENDED_SPLIT);
    {
        SarOamEntry entry;
        sar_oam_decode_extended_split(&entry, fill);
        CHECK_EQ_INT(sar_oam_disabled(&entry), 0);
        CHECK_EQ_INT(entry.x, 512);
        CHECK_EQ_INT(entry.y, 512);
    }
}

/* The rewrite converts the GBA records of a 12-byte table and leaves every
 * other record byte for byte as the engine wrote it. */
static void test_rewrite(void)
{
    uint8_t table[SAR_OAM_ENTRY_COUNT * SAR_OAM_EXTENDED_RECORD_SIZE];
    uint8_t expected[SAR_OAM_EXTENDED_RECORD_SIZE];
    size_t rewritten;
    int i;

    memset(table, 0, sizeof(table));

    /* Entry 0: a GBA `.all` HUD entry, the way the Sonic Advance 1 stage HUD
     * writes it, with stale coordinates left over from the allocator. */
    build_extended_all(&table[0], 0x006Cu, 0x0010u, 0x8005u, 0x0009u, 0x6054u, 0x0000u);

    /* Entry 1: an extended `.split` sprite. */
    build_extended_split(&table[SAR_OAM_EXTENDED_RECORD_SIZE], 108, 16, 0x8005u, 0x0009u, 0x6054u, 0x0000u);

    /* Entry 2: an unused entry with the engine's fill pattern. */
    for (i = 0; i < 6; i++) {
        put_u16(&table[2 * SAR_OAM_EXTENDED_RECORD_SIZE], i * 2, 0x0200u);
    }

    /* Entry 3: a hidden GBA record, which is not drawn in either view. */
    build_extended_all(&table[3 * SAR_OAM_EXTENDED_RECORD_SIZE], 0x006Cu, 0x0010u, 0x0210u, 0x0020u, 0x0000u, 0x0000u);

    /* Entry 4: an ambiguous record, which the rewrite leaves alone. */
    build_extended_all(&table[4 * SAR_OAM_EXTENDED_RECORD_SIZE], 0x006Cu, 0x0010u, 0x0000u, 0x0000u, 0x3523u, 0x0000u);

    rewritten = sar_oam_rewrite_extended(table, SAR_OAM_ENTRY_COUNT, NULL, widescreen);
    CHECK_EQ_INT(rewritten, 1);

    /* The HUD entry now holds the extended projection of its GBA words. */
    build_extended_split(expected, 9, 5, 0x8005u, 0x0009u, 0x6054u, 0x0000u);
    CHECK_EQ_INT(memcmp(&table[0], expected, SAR_OAM_EXTENDED_RECORD_SIZE), 0);

    /* The sprite entry is untouched. */
    build_extended_split(expected, 108, 16, 0x8005u, 0x0009u, 0x6054u, 0x0000u);
    CHECK_EQ_INT(memcmp(&table[SAR_OAM_EXTENDED_RECORD_SIZE], expected, SAR_OAM_EXTENDED_RECORD_SIZE), 0);

    /* The unused entry is untouched, including its fill pattern. */
    for (i = 0; i < 12; i++) {
        CHECK_EQ_INT(table[2 * SAR_OAM_EXTENDED_RECORD_SIZE + i], (i % 2) ? 0x02 : 0x00);
    }

    /* The hidden record is untouched as well. */
    for (i = 0; i < 12; i++) {
        static const uint8_t hidden[12] = {0x6C, 0x00, 0x10, 0x00, 0x10, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};
        CHECK_EQ_INT(table[3 * SAR_OAM_EXTENDED_RECORD_SIZE + i], hidden[i]);
    }

    /* The ambiguous record is untouched as well. */
    for (i = 0; i < 12; i++) {
        static const uint8_t ambiguous[12] = {0x6C, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x35, 0x00, 0x00};
        CHECK_EQ_INT(table[4 * SAR_OAM_EXTENDED_RECORD_SIZE + i], ambiguous[i]);
    }
}

/* The canonical table keeps the 128 entries and their order. */
static void test_table(void)
{
    uint8_t table[SAR_OAM_ENTRY_COUNT * SAR_OAM_EXTENDED_RECORD_SIZE];
    SarOamTable canonical;
    int i;

    for (i = 0; i < SAR_OAM_ENTRY_COUNT; i++) {
        build_extended_split(&table[i * SAR_OAM_EXTENDED_RECORD_SIZE], i, i + 1, 0x0000u, 0x8000u, (unsigned int)i, 0x0000u);
    }

    sar_oam_table_decode(&canonical, table, SAR_OAM_EXTENDED_RECORD_SIZE, SAR_OAM_ENTRY_COUNT, NULL, widescreen);

    for (i = 0; i < SAR_OAM_ENTRY_COUNT; i++) {
        CHECK_EQ_INT(canonical.entries[i].x, i);
        CHECK_EQ_INT(canonical.entries[i].y, i + 1);
        CHECK_EQ_INT(sar_oam_tile(&canonical.entries[i]), (unsigned int)i);
        CHECK_EQ_INT(canonical.views[i], SAR_OAM_VIEW_EXTENDED_SPLIT);
    }
}

int main(void)
{
    test_normal_sprite();
    test_affine_sprite();
    test_hidden_sprite();
    test_flips();
    test_mosaic_and_bpp();
    test_priority_palette_tile();
    test_shape_and_size();
    test_negative_and_wrapped_coordinates();
    test_round_trip();
    test_writer_styles_agree();
    test_classification();
    test_rewrite();
    test_table();

    printf("oam_test: %d checks, %d failures\n", checks, failures);

    if (failures != 0) {
        printf("oam_test: FAILED\n");
        return 1;
    }

    printf("oam_test: ok\n");
    return 0;
}
