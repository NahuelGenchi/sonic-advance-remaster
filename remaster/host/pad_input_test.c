/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Standalone tests for the stage 0 controller mapping. Build and run with
 * tools/pad_test.sh; no display, ROM or upstream checkout is needed.
 */

#include <stdio.h>

#include "pad_input.h"

static int checks;
static int failures;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("pad_test: FAIL: %s\n", what);
    }
}

static uint32_t bit(SarPadButton button)
{
    return (uint32_t)1u << (unsigned int)button;
}

int main(void)
{
    SarPadState state;
    int button;

    SarPadClear(&state);
    check(SarPadButtonMask(&state) == 0, "a cleared state has no buttons");
    check(state.axis_x == 0 && state.axis_y == 0, "a cleared state has centred axes");
    check(state.trigger_l == 0 && state.trigger_r == 0, "a cleared state has released triggers");

    for (button = 0; button < SAR_PAD_BUTTON_COUNT; button++) {
        SarPadClear(&state);
        SarPadSetButton(&state, (SarPadButton)button, 1);
        check(SarPadButtonMask(&state) == bit((SarPadButton)button), "one pressed button produces its bit");
        SarPadSetButton(&state, (SarPadButton)button, 0);
        check(SarPadButtonMask(&state) == 0, "releasing the button clears its bit");
    }

    SarPadClear(&state);
    SarPadSetButton(&state, SAR_PAD_BUTTON_COUNT, 1);
    check(SarPadButtonMask(&state) == 0, "an out-of-range button is ignored");
    SarPadSetButton(&state, (SarPadButton)-1, 1);
    check(SarPadButtonMask(&state) == 0, "a negative button is ignored");

    SarPadClear(&state);
    SarPadSetAxes(&state, -SAR_PAD_DEADZONE, 0);
    check(SarPadButtonMask(&state) == bit(SAR_PAD_LEFT), "the stick reaches left at the deadzone");
    SarPadSetAxes(&state, -SAR_PAD_DEADZONE + 1, 0);
    check(SarPadButtonMask(&state) == 0, "the stick ignores just inside the deadzone");
    SarPadSetAxes(&state, SAR_PAD_DEADZONE, 0);
    check(SarPadButtonMask(&state) == bit(SAR_PAD_RIGHT), "the stick reaches right at the deadzone");
    SarPadSetAxes(&state, SAR_PAD_DEADZONE - 1, 0);
    check(SarPadButtonMask(&state) == 0, "the stick ignores just inside the right deadzone");

    SarPadSetAxes(&state, 0, -SAR_PAD_DEADZONE);
    check(SarPadButtonMask(&state) == bit(SAR_PAD_UP), "the stick reaches up at the deadzone");
    SarPadSetAxes(&state, 0, -SAR_PAD_DEADZONE + 1);
    check(SarPadButtonMask(&state) == 0, "the stick ignores just inside the up deadzone");
    SarPadSetAxes(&state, 0, SAR_PAD_DEADZONE);
    check(SarPadButtonMask(&state) == bit(SAR_PAD_DOWN), "the stick reaches down at the deadzone");
    SarPadSetAxes(&state, 0, SAR_PAD_DEADZONE - 1);
    check(SarPadButtonMask(&state) == 0, "the stick ignores just inside the down deadzone");

    SarPadClear(&state);
    SarPadSetAxes(&state, -32768, -32768);
    check(SarPadButtonMask(&state) == (bit(SAR_PAD_LEFT) | bit(SAR_PAD_UP)), "a diagonal stick presses two directions");
    SarPadSetAxes(&state, 32767, 32767);
    check(SarPadButtonMask(&state) == (bit(SAR_PAD_RIGHT) | bit(SAR_PAD_DOWN)), "the opposite diagonal presses two directions");

    SarPadClear(&state);
    SarPadSetTriggers(&state, SAR_PAD_TRIGGER_THRESHOLD - 1, 0);
    check(SarPadButtonMask(&state) == 0, "the left trigger ignores a value below the threshold");
    SarPadSetTriggers(&state, SAR_PAD_TRIGGER_THRESHOLD, 0);
    check(SarPadButtonMask(&state) == bit(SAR_PAD_L), "the left trigger reaches L at the threshold");
    SarPadSetTriggers(&state, 32767, 0);
    check(SarPadButtonMask(&state) == bit(SAR_PAD_L), "the left trigger stays L at full travel");
    SarPadSetTriggers(&state, 0, SAR_PAD_TRIGGER_THRESHOLD);
    check(SarPadButtonMask(&state) == bit(SAR_PAD_R), "the right trigger reaches R at the threshold");
    SarPadSetTriggers(&state, SAR_PAD_TRIGGER_THRESHOLD, SAR_PAD_TRIGGER_THRESHOLD);
    check(SarPadButtonMask(&state) == (bit(SAR_PAD_L) | bit(SAR_PAD_R)), "both triggers can be held");

    SarPadClear(&state);
    SarPadSetButton(&state, SAR_PAD_A, 1);
    SarPadSetAxes(&state, SAR_PAD_DEADZONE, -SAR_PAD_DEADZONE);
    SarPadSetTriggers(&state, 0, SAR_PAD_TRIGGER_THRESHOLD);
    check(SarPadButtonMask(&state)
              == (bit(SAR_PAD_A) | bit(SAR_PAD_RIGHT) | bit(SAR_PAD_UP) | bit(SAR_PAD_R)),
          "buttons, stick and triggers combine");

    SarPadClear(&state);
    check(SarPadButtonMask(&state) == 0, "repeated clears stay empty");

    printf("pad_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
