/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Scripted input for deterministic recorded runs. See
 * remaster/host/script_input.h.
 */

#include "script_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"

static int script_sa1;

int SarScriptEnabled(void)
{
    return script_sa1;
}

uint16_t SarScriptedKeys(unsigned long long frame)
{
    uint16_t keys = 0;
    unsigned int index;

    if (!script_sa1) {
        return 0;
    }

    index = (unsigned int)frame;

    if (index >= 600 && index < 1100) {
        /* Boot, title and menus: alternate A and START. */
        if ((index % 40) < 5) {
            keys |= A_BUTTON;
        }
        if ((index % 40) >= 20 && (index % 40) < 25) {
            keys |= START_BUTTON;
        }
    } else if (index >= 1100) {
        /* Run right and jump; no START, which pauses a running stage. */
        keys |= DPAD_RIGHT;
        if ((index % 150) < 6) {
            keys |= A_BUTTON;
        }
    }

    return keys;
}

/*
 * The script is selected once, at start-up, so that a recorded run cannot
 * change its input halfway through.
 */
void SarScriptInit(void)
{
    const char *script = getenv("SAR_SCRIPT");

    if (script == NULL || script[0] == '\0') {
        return;
    }

    if (strcmp(script, "sa1") == 0) {
        script_sa1 = 1;
        printf("stage0: playing the Sonic Advance 1 capture script\n");
        return;
    }

    fprintf(stderr, "stage0: unknown SAR_SCRIPT: %s\n", script);
}
