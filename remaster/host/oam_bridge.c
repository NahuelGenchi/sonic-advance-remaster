/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The OAM boundary between the upstream engine and the remaster renderer.
 * See remaster/host/oam_bridge.h for what it does and why.
 */

#include "oam_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"

static SarOamTable canonical_table;
static size_t converted_entries;
static int bridge_enabled = 1;

/*
 * The upstream build always uses the extended 12-byte record. The canonical
 * module knows the same layout, and the check fails the build instead of
 * silently converting the wrong bytes if that ever changes.
 */
typedef char SarOamLayoutCheck[(OAM_DATA_SIZE_AFFINE == SAR_OAM_EXTENDED_RECORD_SIZE) ? 1 : -1];

void SarOamBridgeInit(void)
{
    const char *value = getenv("SAR_OAM_BRIDGE");

    if (value != NULL && strcmp(value, "0") == 0) {
        bridge_enabled = 0;
        printf("oam_bridge: disabled by SAR_OAM_BRIDGE=0\n");
    }

    memset(&canonical_table, 0, sizeof(canonical_table));
    converted_entries = 0;
}

void SarOamBridgeCapture(void)
{
    SarOamLimits limits;

    if (!bridge_enabled) {
        return;
    }

    limits.width = (int16_t)DISPLAY_WIDTH;
    limits.height = (int16_t)DISPLAY_HEIGHT;

    /*
     * The canonical table is the renderer's view of the frame. It is decoded
     * from the engine's records before the rewrite, so the writer style of
     * every entry is decided by the bytes the engine wrote.
     */
    sar_oam_table_decode(&canonical_table, OAM, OAM_DATA_SIZE_AFFINE, OAM_ENTRY_COUNT, NULL, limits);

    /*
     * The software PPU reads the extended view only, so the GBA `.all`
     * records are converted in place. The engine's staging buffer is not
     * touched, and the engine overwrites the OAM buffer on its next VBlank.
     */
    converted_entries = sar_oam_rewrite_extended(OAM, OAM_ENTRY_COUNT, NULL, limits);
}

const SarOamTable *SarOamBridgeTable(void)
{
    return &canonical_table;
}

size_t SarOamBridgeConvertedCount(void)
{
    return converted_entries;
}
