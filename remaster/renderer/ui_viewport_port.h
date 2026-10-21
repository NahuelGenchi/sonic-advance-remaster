/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The upstream adapter of the UI viewport. See ui_viewport_port.c for what
 * the seam owns and why it exists.
 */

#ifndef SAR_RENDERER_UI_VIEWPORT_PORT_H
#define SAR_RENDERER_UI_VIEWPORT_PORT_H

#include "ui_viewport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The scene the engine is in. The engine has no scene flag, so the seam
 * reads engine state the screens set: the stage task (which also covers the
 * stage intro), and in Sonic Advance 2 the display mode, where mode 1 marks
 * the screens authored for the 426x240 world and mode 0 marks the screens
 * authored on the 240x160 field.
 */
SarUiScene sar_ui_viewport_port_scene(void);

/* The field of the last composed frame. */
const uint16_t *sar_ui_viewport_port_field(void);

/*
 * Compose the field. The engine's H-blank handler runs while the field is
 * composed, so the per-scanline scroll registers are the ones the field is
 * sampled with.
 */
void sar_ui_viewport_port_render_field(void);

#ifdef __cplusplus
}
#endif

#endif /* SAR_RENDERER_UI_VIEWPORT_PORT_H */
