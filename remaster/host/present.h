/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 0 presentation path: upload the upstream software PPU frame to an
 * OpenGL 3.3 core texture and draw it at 426x240, scaled and letterboxed to the
 * window's drawable size.
 */

#ifndef SAR_PRESENT_H
#define SAR_PRESENT_H

#include <SDL.h>
#include <stdint.h>

int SarPresentInit(SDL_Window *window);
void SarPresentFrame(const uint16_t *pixels, int width, int height);
int SarPresentCaptureGlPpm(const char *path);
void SarPresentSwap(void);
void SarPresentShutdown(void);

#endif // SAR_PRESENT_H
