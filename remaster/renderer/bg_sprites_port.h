/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapter between the upstream game state and the project-owned BG-sprite
 * composition module (bg_sprites.c).
 *
 * The adapter is the only file in this directory that includes upstream
 * headers. It never modifies upstream state: it reads the globals the game
 * maintains (gBgSprites, gBgSpritesCount, gBgSprites_Unknown1/2, gBgCntRegs,
 * gDispCnt) and hands a snapshot to the composition module.
 *
 * VBlank order (the order the host must call these in):
 *
 *   1. the game tick has written VRAM, the character data of the registered
 *      BG sprites is queued, and the fill state is set;
 *   2. SarBgSpritesBeginVBlank() snapshots that state;
 *   3. the VBlank interrupt runs (the queued character data is copied into
 *      VRAM and upstream clears its own fill state);
 *   4. SarBgSpritesCompose() writes the tilemap entries;
 *   5. the software PPU draws the frame.
 *
 * Step 2 has to happen before step 3 because upstream's VBlank handlers clear
 * the state this module reads. Step 4 has to happen after step 3 so the map
 * is written after the character data it references, and before step 5 so the
 * frame shows the composed map.
 */

#ifndef SAR_RENDERER_BG_SPRITES_PORT_H
#define SAR_RENDERER_BG_SPRITES_PORT_H

#include "renderer/bg_sprites.h"

/* Snapshot the BG-sprite state the game registered during this tick. */
void SarBgSpritesBeginVBlank(void);

/* Apply the fills and compose the registered BG sprites into BG VRAM. */
void SarBgSpritesCompose(void);

/* Counters of the last composed frame. */
const SarBgStats *SarBgSpritesStats(void);

#endif /* SAR_RENDERER_BG_SPRITES_PORT_H */
