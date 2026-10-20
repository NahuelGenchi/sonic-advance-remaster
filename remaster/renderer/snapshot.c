/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 1 snapshot capture.
 *
 * This file runs the scanline state machine that the upstream software PPU
 * would run, and records the display state of every scanline. It never draws
 * anything: the software PPU stays the reference and the fallback, and it is
 * not called from the GPU path.
 *
 * The order below mirrors gpsp_draw_frame in the upstream software PPU:
 *
 *   set VCOUNT, dispatch the VCount interrupt, reload the affine reference
 *   counters, render the line, then run the HBlank DMA and the HBlank
 *   interrupt, then step the affine references for the next line.
 *
 * The snapshot records the state the PPU would have used for the line, that
 * is, before the HBlank DMA of that line runs. That is what makes the Leaf
 * Forest BG3 per-scanline scroll effect reproducible.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "core.h"
#include "gba/defines.h"
#include "gba/io_reg.h"
#include "gba/types.h"
#include "platform/shared/dma.h"

#include "snapshot.h"

static int32_t s_affine_x[2];
static int32_t s_affine_y[2];
static uint32_t s_prev_affine[4];
static uint32_t s_frame;

static int32_t SignExtend28(uint32_t value)
{
    return (int32_t)(value << 4) >> 4;
}

static uint32_t ReadIo32(const void *address)
{
    const volatile uint16_t *half = (const volatile uint16_t *)address;

    return (uint32_t)half[0] | ((uint32_t)half[1] << 16);
}

// Same rule as the upstream PPU: the affine reference registers are reloaded
// when the game changes them, and unconditionally at the start of a frame.
static void ReloadAffineCounters(int vblank)
{
    uint32_t value;

    value = ReadIo32(REG_ADDR_BG2X_L);
    if (vblank || s_prev_affine[0] != value) {
        s_affine_x[0] = SignExtend28(value);
        s_prev_affine[0] = value;
    }

    value = ReadIo32(REG_ADDR_BG2Y_L);
    if (vblank || s_prev_affine[1] != value) {
        s_affine_y[0] = SignExtend28(value);
        s_prev_affine[1] = value;
    }

    value = ReadIo32(REG_ADDR_BG3X_L);
    if (vblank || s_prev_affine[2] != value) {
        s_affine_x[1] = SignExtend28(value);
        s_prev_affine[2] = value;
    }

    value = ReadIo32(REG_ADDR_BG3Y_L);
    if (vblank || s_prev_affine[3] != value) {
        s_affine_y[1] = SignExtend28(value);
        s_prev_affine[3] = value;
    }
}

// Steps the affine reference counters to the next scanline, honouring the
// vertical mosaic of the upstream PPU.
static void StepAffineCounters(uint32_t vcount, uint16_t dispcnt)
{
    uint32_t video_mode = dispcnt & 0x07;
    uint32_t bg_mosaic_v;

    if (!video_mode) {
        return;
    }

    bg_mosaic_v = ((REG_MOSAIC >> 4) & 0xF) + 1;

    if (REG_BG2CNT & 0x40) {
        if ((vcount % bg_mosaic_v) == bg_mosaic_v - 1) {
            s_affine_x[0] += (int16_t)REG_BG2PB * (int32_t)bg_mosaic_v;
            s_affine_y[0] += (int16_t)REG_BG2PD * (int32_t)bg_mosaic_v;
        }
    } else {
        s_affine_x[0] += (int16_t)REG_BG2PB;
        s_affine_y[0] += (int16_t)REG_BG2PD;
    }

    if (REG_BG3CNT & 0x40) {
        if ((vcount % bg_mosaic_v) == bg_mosaic_v - 1) {
            s_affine_x[1] += (int16_t)REG_BG3PB * (int32_t)bg_mosaic_v;
            s_affine_y[1] += (int16_t)REG_BG3PD * (int32_t)bg_mosaic_v;
        }
    } else {
        s_affine_x[1] += (int16_t)REG_BG3PB;
        s_affine_y[1] += (int16_t)REG_BG3PD;
    }
}

static void CaptureLine(SarSnapshotLine *line)
{
    static const void *cnt_addr[SAR_SNAP_BGS] = {
        REG_ADDR_BG0CNT,
        REG_ADDR_BG1CNT,
        REG_ADDR_BG2CNT,
        REG_ADDR_BG3CNT,
    };
    static const void *hofs_addr[SAR_SNAP_BGS] = {
        REG_ADDR_BG0HOFS,
        REG_ADDR_BG1HOFS,
        REG_ADDR_BG2HOFS,
        REG_ADDR_BG3HOFS,
    };
    static const void *vofs_addr[SAR_SNAP_BGS] = {
        REG_ADDR_BG0VOFS,
        REG_ADDR_BG1VOFS,
        REG_ADDR_BG2VOFS,
        REG_ADDR_BG3VOFS,
    };
    // Only BG2 and BG3 have affine registers; the text layers keep zeroes.
    static const void *pa_addr[2] = { REG_ADDR_BG2PA, REG_ADDR_BG3PA };
    static const void *pb_addr[2] = { REG_ADDR_BG2PB, REG_ADDR_BG3PB };
    static const void *pc_addr[2] = { REG_ADDR_BG2PC, REG_ADDR_BG3PC };
    static const void *pd_addr[2] = { REG_ADDR_BG2PD, REG_ADDR_BG3PD };
    int i;

    for (i = 0; i < SAR_SNAP_BGS; i++) {
        line->cnt[i] = *(const volatile uint16_t *)cnt_addr[i];
        line->hofs[i] = *(const volatile uint16_t *)hofs_addr[i];
        line->vofs[i] = *(const volatile uint16_t *)vofs_addr[i];
    }

    for (i = 0; i < 2; i++) {
        line->pa[i + 2] = (int16_t) * (const volatile uint16_t *)pa_addr[i];
        line->pb[i + 2] = (int16_t) * (const volatile uint16_t *)pb_addr[i];
        line->pc[i + 2] = (int16_t) * (const volatile uint16_t *)pc_addr[i];
        line->pd[i + 2] = (int16_t) * (const volatile uint16_t *)pd_addr[i];
    }

    line->aref_x[0] = s_affine_x[0];
    line->aref_y[0] = s_affine_y[0];
    line->aref_x[1] = s_affine_x[1];
    line->aref_y[1] = s_affine_y[1];

    line->dispcnt = REG_DISPCNT;
    line->bldcnt = REG_BLDCNT;
    line->bldalpha = REG_BLDALPHA;
    line->bldy = REG_BLDY;
    line->mosaic = REG_MOSAIC;
    line->winin = (uint16_t)(*(const volatile uint32_t *)REG_ADDR_WININ & 0xFFFFu);
    line->winout = (uint16_t)(*(const volatile uint32_t *)REG_ADDR_WINOUT & 0xFFFFu);
    line->win0h = *(const volatile uint32_t *)REG_ADDR_WIN0H;
    line->win0v = *(const volatile uint32_t *)REG_ADDR_WIN0V;
    line->win1h = *(const volatile uint32_t *)REG_ADDR_WIN1H;
    line->win1v = *(const volatile uint32_t *)REG_ADDR_WIN1V;
}

void SarSnapshotInit(void)
{
    memset(s_affine_x, 0, sizeof(s_affine_x));
    memset(s_affine_y, 0, sizeof(s_affine_y));
    memset(s_prev_affine, 0, sizeof(s_prev_affine));
    s_frame = 0;
}

int SarSnapshotCapture(SarSnapshot *snapshot)
{
    int i;

    if (snapshot == NULL) {
        return 0;
    }

    if ((uint32_t)OAM_SIZE != (uint32_t)SAR_SNAP_OAM_BYTES) {
        fprintf(stderr, "stage1: unexpected OAM size %u\n", (unsigned)OAM_SIZE);
        return 0;
    }

    snapshot->frame = s_frame++;
    snapshot->vram = VRAM;
    snapshot->vram_size = (uint32_t)VRAM_SIZE;

    ReloadAffineCounters(1);

    for (i = 0; i < DISPLAY_HEIGHT; i++) {
        REG_VCOUNT = (uint16_t)i;
        if (((REG_DISPSTAT >> 8) & 0xFF) == REG_VCOUNT) {
            REG_DISPSTAT |= INTR_FLAG_VCOUNT;
            if (REG_DISPSTAT & DISPSTAT_VCOUNT_INTR) {
                gIntrTable[INTR_INDEX_VCOUNT]();
            }
        }

        ReloadAffineCounters(0);
        CaptureLine(&snapshot->line[i]);

        REG_DISPSTAT |= INTR_FLAG_HBLANK;
        RunDMAs(DMA_HBLANK);
        if (REG_DISPSTAT & DISPSTAT_HBLANK_INTR) {
            gIntrTable[INTR_INDEX_HBLANK]();
        }
        REG_DISPSTAT &= (uint16_t)~INTR_FLAG_HBLANK;
        REG_DISPSTAT &= (uint16_t)~INTR_FLAG_VCOUNT;

        StepAffineCounters((uint32_t)i, snapshot->line[i].dispcnt);
    }

    ReloadAffineCounters(1);

    memcpy(snapshot->oam, OAM, sizeof(snapshot->oam));
    memcpy(snapshot->palette, PLTT, sizeof(snapshot->palette));

    return 1;
}

void SarSnapshotDump(const SarSnapshot *snapshot, FILE *out)
{
    int i;

    if (snapshot == NULL || out == NULL) {
        return;
    }

    fprintf(out, "frame %u\n", snapshot->frame);
    for (i = 0; i < SAR_SNAP_LINES; i++) {
        const SarSnapshotLine *line = &snapshot->line[i];

        fprintf(out,
                "line %3d dispcnt=%04x bldcnt=%04x bldalpha=%04x bldy=%04x mosaic=%04x winin=%04x winout=%04x\n",
                i, line->dispcnt, line->bldcnt, line->bldalpha, line->bldy, line->mosaic, line->winin, line->winout);
        fprintf(out,
                "        bg0 hofs=%5u vofs=%5u cnt=%04x | bg1 hofs=%5u vofs=%5u cnt=%04x | bg2 hofs=%5u vofs=%5u cnt=%04x | bg3 hofs=%5u vofs=%5u cnt=%04x\n",
                line->hofs[0], line->vofs[0], line->cnt[0], line->hofs[1], line->vofs[1], line->cnt[1], line->hofs[2],
                line->vofs[2], line->cnt[2], line->hofs[3], line->vofs[3], line->cnt[3]);
        fprintf(out, "        aref bg2=(%d,%d) bg3=(%d,%d)\n", line->aref_x[0], line->aref_y[0], line->aref_x[1],
                line->aref_y[1]);
    }
    fflush(out);
}
