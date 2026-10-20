/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The OAM boundary between the upstream engine and the remaster renderer.
 *
 * The engine writes two different OAM record layouts in the same build: the
 * GBA-native 8-byte `.all` view (Sonic Advance 1 stage HUD and UI text) and
 * the extended 12-byte `.split` view (Sonic Advance 2 and the widescreen
 * sprite path). The software PPU reads the extended view only, so the GBA
 * records are invisible to it.
 *
 * This bridge is the one place where the two views meet. Once per VBlank it
 *
 *   1. decodes the engine's OAM table into the canonical entries of
 *      remaster/renderer/oam.h, which is what the renderer consumes, and
 *   2. rewrites the GBA `.all` records of the engine's OAM buffer into the
 *      extended projection, in place, so the software PPU and the GPU path
 *      read one layout.
 *
 * The rewrite keeps the 128 entries, their order, the affine parameter words
 * and every record that is not a GBA `.all` record byte for byte as the
 * engine wrote it. The engine's own staging buffer is not touched, so the
 * next frame starts from the engine's data again.
 */

#ifndef SAR_HOST_OAM_BRIDGE_H
#define SAR_HOST_OAM_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "renderer/oam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read the bridge configuration. Called once during host start-up. */
void SarOamBridgeInit(void);

/* Convert the engine's OAM table for this frame. Called once per VBlank,
 * after the VBlank interrupt and before the software PPU draws. */
void SarOamBridgeCapture(void);

/* The canonical OAM table of the last capture. */
const SarOamTable *SarOamBridgeTable(void);

/* The number of GBA `.all` records converted by the last capture. */
size_t SarOamBridgeConvertedCount(void);

#ifdef __cplusplus
}
#endif

#endif /* SAR_HOST_OAM_BRIDGE_H */
