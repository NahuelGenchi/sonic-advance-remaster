/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The upstream adapter of the UI viewport: the seam between the engine and
 * the project-owned field composer.
 *
 * It owns three things the pure module must not know about:
 *
 *   1. the scene. The engine has no scene flag, so the seam reads engine
 *      state that the screens themselves set:
 *
 *        - the stage task, which CreateGameStage creates and the stage
 *          destroys. It also covers the stage intro, which the stage
 *          creates, so the intro and the game stay on the 426x240 world;
 *        - in Sonic Advance 2, the display mode. The title screen and the
 *          multiplayer screens switch the GBA to mode 1 (the affine BG2
 *          mode) and lay their elements out on the 426x240 frame, so they
 *          stay on the world. The language select, character select and
 *          options screens stay in mode 0 and author their maps on the
 *          240x160 field, so they are composed on the field.
 *
 *      Sonic Advance 1 keeps the stage-task rule: its menus and select
 *      screens were verified on the field, and the display mode is not
 *      needed to separate them from its stages.
 *
 *   2. the scanline loop. The field is composed while the engine's H-blank
 *      handler runs, so the per-scanline scroll registers the engine writes
 *      are the ones the field is sampled with. The loop is the same one the
 *      upstream software PPU runs, with the field's scanline in place of the
 *      software pixel loop.
 *
 *   3. the engine state: VRAM, the palette, the OAM and the background
 *      registers.
 *
 * This is the only file of the module that includes upstream headers.
 */

#include "ui_viewport_port.h"

#include <string.h>

#include "global.h"
#include "core.h"
#include "gba/io_reg.h"
#include "gba/syscall.h"
#include "platform/shared/dma.h"
#include "game/shared/stage/stage.h"

#include "renderer/oam.h"

static uint16_t ui_field[SAR_UI_FIELD_WIDTH * SAR_UI_FIELD_HEIGHT];
static SarOamTable ui_oam;

/* The background control and scroll registers, in the hardware's order. */
static uint16_t *const bg_cnt_registers[4] = { &REG_BG0CNT, &REG_BG1CNT, &REG_BG2CNT, &REG_BG3CNT };
static uint16_t *const bg_scroll_registers[8] = {
    &REG_BG0HOFS, &REG_BG0VOFS, &REG_BG1HOFS, &REG_BG1VOFS, &REG_BG2HOFS, &REG_BG2VOFS, &REG_BG3HOFS, &REG_BG3VOFS
};

/* The 28-bit signed affine reference, as the hardware stores it. */
static int32_t sign_extend_28(uint32_t value)
{
    return (int32_t)((value << 4) >> 4);
}

SarUiScene sar_ui_viewport_port_scene(void)
{
    /*
     * The stage task is created by CreateGameStage, which also creates the
     * stage intro, and destroyed when the stage ends. It is the engine's
     * statement that the 426x240 world, not a menu, is on screen.
     */
    if (gGameStageTask != NULL) {
        return SAR_UI_SCENE_GAMEPLAY;
    }

#if (GAME == GAME_SA2)
    /*
     * The title screen (title_screen.c sets DISPCNT_MODE_1) and the
     * multiplayer screens (mode_select.c and multiplayer_lobby.c build
     * their display control on mode 1) are authored for the widescreen
     * frame: the title's logo, PRESS START and cloud scroll are positioned
     * against DISPLAY_WIDTH, and its text backgrounds load DISPLAY_WIDTH/8
     * tile columns. The language select, character select and options
     * screens set gDispCnt to 0x1740, whose mode bits are 0: they author
     * 30x20 and 32x32 maps on the 240x160 field, and composing them there
     * keeps the sampler inside the maps instead of wrapping them at 256
     * columns or reading rows the screen never wrote.
     */
    if ((gDispCnt & 0x7) == DISPCNT_MODE_1) {
        return SAR_UI_SCENE_GAMEPLAY;
    }
#endif

    return SAR_UI_SCENE_FIELD;
}

const uint16_t *sar_ui_viewport_port_field(void)
{
    return ui_field;
}

static void capture_oam(void)
{
    SarOamLimits limits;

    limits.width = (int16_t)DISPLAY_WIDTH;
    limits.height = (int16_t)DISPLAY_HEIGHT;

    sar_oam_table_decode(&ui_oam, OAM, OAM_DATA_SIZE_AFFINE, OAM_ENTRY_COUNT, NULL, limits);
}

static void capture_registers(SarUiViewportState *state)
{
    int bg;

    state->dispcnt = REG_DISPCNT;
    for (bg = 0; bg < 4; bg++) {
        state->bgcnt[bg] = *bg_cnt_registers[bg];
        state->bgofs_h[bg] = *bg_scroll_registers[bg * 2];
        state->bgofs_v[bg] = *bg_scroll_registers[bg * 2 + 1];
    }
}

static void capture_affine(SarUiViewportState *state)
{
    state->bg2_x = sign_extend_28(REG_BG2X);
    state->bg2_y = sign_extend_28(REG_BG2Y);
    state->bg2_pa = (int16_t)REG_BG2PA;
    state->bg2_pb = (int16_t)REG_BG2PB;
    state->bg2_pc = (int16_t)REG_BG2PC;
    state->bg2_pd = (int16_t)REG_BG2PD;
    state->bg3_x = sign_extend_28(REG_BG3X);
    state->bg3_y = sign_extend_28(REG_BG3Y);
    state->bg3_pa = (int16_t)REG_BG3PA;
    state->bg3_pb = (int16_t)REG_BG3PB;
    state->bg3_pc = (int16_t)REG_BG3PC;
    state->bg3_pd = (int16_t)REG_BG3PD;
}

void sar_ui_viewport_port_render_field(void)
{
    SarUiViewportState state;
    int line;

    memset(&state, 0, sizeof(state));
    state.vram = VRAM;
    state.palette = (const uint16_t *)PLTT;

    capture_oam();
    state.oam = &ui_oam;
    capture_registers(&state);
    capture_affine(&state);

    /*
     * The affine parameters are the ones the engine wrote for this frame.
     * The reference is the source pixel the screen's origin maps to; it
     * steps by the vertical matrix after every line, the way the hardware
     * does, so the field is sampled with the same source pixels the
     * software PPU samples.
     */
    for (line = 0; line < DISPLAY_HEIGHT; line++) {
        REG_VCOUNT = (uint16_t)line;

        if (((REG_DISPSTAT >> 8) & 0xFF) == REG_VCOUNT) {
            REG_DISPSTAT |= INTR_FLAG_VCOUNT;
            if (REG_DISPSTAT & DISPSTAT_VCOUNT_INTR) {
                gIntrTable[INTR_INDEX_VCOUNT]();
            }
        }

        if (line < SAR_UI_FIELD_HEIGHT) {
            /* The engine's H-blank handler may have changed the scroll
             * registers, so they are read again for every line. */
            capture_registers(&state);
            sar_ui_viewport_render_scanline(&state, SAR_UI_MAP_LAYOUT_GBA, line, ui_field + (size_t)line * SAR_UI_FIELD_WIDTH);

            /* The reference steps after the line it was used for. */
            state.bg2_x += state.bg2_pb;
            state.bg2_y += state.bg2_pd;
            state.bg3_x += state.bg3_pb;
            state.bg3_y += state.bg3_pd;
        }

        REG_DISPSTAT |= INTR_FLAG_HBLANK;

        RunDMAs(DMA_HBLANK);

        if (REG_DISPSTAT & DISPSTAT_HBLANK_INTR) {
            gIntrTable[INTR_INDEX_HBLANK]();
        }

        REG_DISPSTAT &= (uint16_t)~INTR_FLAG_HBLANK;
        REG_DISPSTAT &= (uint16_t)~INTR_FLAG_VCOUNT;
    }
}
