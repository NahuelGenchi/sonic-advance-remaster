/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 1 snapshot interface.
 *
 * The snapshot is the only thing the GPU layer path is allowed to see. It is
 * captured once per game tick, at the same point where the upstream software
 * PPU would start drawing the frame: after the VBlank interrupt ran and the
 * VBlank DMA copied the display registers, before the game prepares the next
 * frame.
 *
 * The scanline state machine in snapshot.c follows the order of the upstream
 * software PPU (gpsp_draw_frame): set VCOUNT, step the affine reference
 * counters, then run the HBlank DMA and the HBlank interrupt, and only then
 * record the state of the next line. The software PPU stays the reference and
 * the fallback; it is not called from the GPU path.
 */

#ifndef SAR_SNAPSHOT_H
#define SAR_SNAPSHOT_H

#include <stdint.h>
#include <stdio.h>

#define SAR_SNAP_LINES 240
#define SAR_SNAP_BGS 4
#define SAR_SNAP_PALETTE_ENTRIES 512
#define SAR_SNAP_OAM_BYTES 1536

// One scanline of display state. The renderer reads these per line, so a
// register that the game changes during HBlank is reproduced exactly.
typedef struct SarSnapshotLine {
    uint16_t hofs[SAR_SNAP_BGS];
    uint16_t vofs[SAR_SNAP_BGS];
    uint16_t cnt[SAR_SNAP_BGS];
    int16_t pa[SAR_SNAP_BGS];
    int16_t pb[SAR_SNAP_BGS];
    int16_t pc[SAR_SNAP_BGS];
    int16_t pd[SAR_SNAP_BGS];
    int32_t aref_x[2]; // BG2 and BG3 reference X, in 8.8 fixed point
    int32_t aref_y[2]; // BG2 and BG3 reference Y, in 8.8 fixed point
    uint16_t dispcnt;
    uint16_t bldcnt;
    uint16_t winin;
    uint16_t winout;
    uint16_t mosaic;
    uint16_t bldalpha;
    uint16_t bldy;
    uint32_t win0h;
    uint32_t win0v;
    uint32_t win1h;
    uint32_t win1v;
} SarSnapshotLine;

typedef struct SarSnapshot {
    SarSnapshotLine line[SAR_SNAP_LINES];
    uint8_t oam[SAR_SNAP_OAM_BYTES];
    uint16_t palette[SAR_SNAP_PALETTE_ENTRIES];
    const uint8_t *vram; // points at the live VRAM; consumed immediately
    uint32_t vram_size;
    uint32_t frame;
} SarSnapshot;

// Prepares the snapshot module. Safe to call more than once.
void SarSnapshotInit(void);

// Runs the scanline state machine for one frame and fills the snapshot.
// Returns 0 only if the frame could not be captured at all.
int SarSnapshotCapture(SarSnapshot *snapshot);

// Writes the captured state to a file, for review and debugging.
void SarSnapshotDump(const SarSnapshot *snapshot, FILE *out);

#endif // SAR_SNAPSHOT_H
