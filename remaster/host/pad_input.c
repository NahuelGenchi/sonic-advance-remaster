/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Controller input mapping for the stage 0 host. See pad_input.h.
 */

#include "pad_input.h"

#include <string.h>

void SarPadClear(SarPadState *state)
{
    memset(state, 0, sizeof(*state));
}

void SarPadSetButton(SarPadState *state, SarPadButton button, int pressed)
{
    if ((int)button < 0 || button >= SAR_PAD_BUTTON_COUNT) {
        return;
    }

    state->pressed[button] = (uint8_t)(pressed != 0);
}

void SarPadSetAxes(SarPadState *state, int16_t x, int16_t y)
{
    state->axis_x = x;
    state->axis_y = y;
}

void SarPadSetTriggers(SarPadState *state, int16_t left, int16_t right)
{
    state->trigger_l = left;
    state->trigger_r = right;
}

static uint32_t button_bit(SarPadButton button)
{
    return (uint32_t)1u << (unsigned int)button;
}

uint32_t SarPadButtonMask(const SarPadState *state)
{
    uint32_t mask = 0;
    int button;

    for (button = 0; button < SAR_PAD_BUTTON_COUNT; button++) {
        if (state->pressed[button]) {
            mask |= button_bit((SarPadButton)button);
        }
    }

    if (state->axis_x <= -SAR_PAD_DEADZONE) {
        mask |= button_bit(SAR_PAD_LEFT);
    } else if (state->axis_x >= SAR_PAD_DEADZONE) {
        mask |= button_bit(SAR_PAD_RIGHT);
    }

    if (state->axis_y <= -SAR_PAD_DEADZONE) {
        mask |= button_bit(SAR_PAD_UP);
    } else if (state->axis_y >= SAR_PAD_DEADZONE) {
        mask |= button_bit(SAR_PAD_DOWN);
    }

    if (state->trigger_l >= SAR_PAD_TRIGGER_THRESHOLD) {
        mask |= button_bit(SAR_PAD_L);
    }

    if (state->trigger_r >= SAR_PAD_TRIGGER_THRESHOLD) {
        mask |= button_bit(SAR_PAD_R);
    }

    return mask;
}
