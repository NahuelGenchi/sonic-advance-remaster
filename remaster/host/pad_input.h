/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Controller input mapping for the stage 0 host.
 *
 * This module is deliberately free of SDL and upstream headers: the host
 * translates SDL controller events into a SarPadState, and this module turns
 * that snapshot into the logical buttons the host maps to GBA key bits. The
 * standalone test in pad_input_test.c builds the module without a display,
 * a ROM or an upstream checkout.
 */

#ifndef SAR_HOST_PAD_INPUT_H
#define SAR_HOST_PAD_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Left-stick and trigger thresholds. SDL reports sticks in [-32768, 32767]
 * and triggers in [0, 32767]. */
#define SAR_PAD_DEADZONE 8000
#define SAR_PAD_TRIGGER_THRESHOLD 8000

typedef enum {
    SAR_PAD_UP = 0,
    SAR_PAD_DOWN,
    SAR_PAD_LEFT,
    SAR_PAD_RIGHT,
    SAR_PAD_A,
    SAR_PAD_B,
    SAR_PAD_START,
    SAR_PAD_SELECT,
    SAR_PAD_L,
    SAR_PAD_R,
    SAR_PAD_BUTTON_COUNT
} SarPadButton;

typedef struct {
    uint8_t pressed[SAR_PAD_BUTTON_COUNT];
    int16_t axis_x;
    int16_t axis_y;
    int16_t trigger_l;
    int16_t trigger_r;
} SarPadState;

void SarPadClear(SarPadState *state);
void SarPadSetButton(SarPadState *state, SarPadButton button, int pressed);
void SarPadSetAxes(SarPadState *state, int16_t x, int16_t y);
void SarPadSetTriggers(SarPadState *state, int16_t left, int16_t right);

/* One bit per SarPadButton, with the stick and trigger thresholds applied. */
uint32_t SarPadButtonMask(const SarPadState *state);

#ifdef __cplusplus
}
#endif

#endif /* SAR_HOST_PAD_INPUT_H */
