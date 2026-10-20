/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 1 GPU layer path interface.
 *
 * The layer path draws the GBA layers from a per-tick snapshot with OpenGL
 * 3.3 core. It never changes game state: it reads the snapshot with the VRAM,
 * the palettes and the OAM, and it writes only to its own targets.
 */

#ifndef SAR_LAYER_PATH_H
#define SAR_LAYER_PATH_H

#include <SDL.h>

#include "renderer/snapshot.h"

// Creates the GL resources and compiles the Stage 1 shaders.
// Returns 0 on any failure; the caller must then fall back or fail closed.
int SarLayerPathInit(SDL_Window *window);

// Captures the snapshot for the current tick, uploads it and renders the
// layers and the composite into the scene target.
// Returns 0 when the frame could not be rendered; the caller must not present
// a frame it could not draw.
int SarLayerPathRenderFrame(SarSnapshot *snapshot);

// The GL texture that holds the composited scene, for the present pass.
unsigned int SarLayerPathSceneTexture(void);

// Whether GL timer queries are available in this context.
int SarLayerPathHasGpuTiming(void);

void SarLayerPathShutdown(void);

#endif // SAR_LAYER_PATH_H
