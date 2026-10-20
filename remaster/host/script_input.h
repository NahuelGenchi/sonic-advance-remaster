/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Scripted input for deterministic recorded runs.
 *
 * The stage 0 host is an interactive host: it reads the keyboard. A
 * recorded run of a scripted scene needs the same input schedule the M1
 * capture path uses, so the stage 0 host can replay it.
 *
 * The schedule is the one documented in docs/CAPTURE.md for the Sonic
 * Advance 1 run. It is a verification helper, not a game feature.
 */

#ifndef SAR_HOST_SCRIPT_INPUT_H
#define SAR_HOST_SCRIPT_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read the script selection. Called once during host start-up. */
void SarScriptInit(void);

/* Non-zero when a script is selected with SAR_SCRIPT. */
int SarScriptEnabled(void);

/* The key state the selected script presses on the given VBlank. */
uint16_t SarScriptedKeys(unsigned long long frame);

#ifdef __cplusplus
}
#endif

#endif /* SAR_HOST_SCRIPT_INPUT_H */
