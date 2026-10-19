/*
 * Renderer harness host.
 *
 * This is not a renderer. It is the smallest executable that proves the M2
 * integration boundary: an upstream translation unit (the software GBA PPU)
 * is compiled from an external SAT-R/sa2 checkout, archived, and linked into a
 * host that this repository owns. The host calls the upstream PPU through its
 * public C symbol and checks the resulting framebuffer.
 *
 * The probe stubs only the globals the upstream PPU expects from the game
 * platform: the interrupt table and the DMA entry point. In the full port those
 * come from the core archive; here they keep the harness independent of a ROM,
 * SDL, and the rest of the game.
 */

#include <stdint.h>
#include <stdio.h>

#include "global.h"
#include "core.h"
#include "gba/io_reg.h"
#include "platform/shared/dma.h"
#include "platform/shared/video/gpsp_renderer.h"

IntrFunc gIntrTable[16];

void RunDMAs(DmaStartTypes type) { (void)type; }

void IntrMain(void);

static int vblankCount;

static void CountVBlank(void) { vblankCount++; }

static uint32_t Checksum(const uint16_t *pixels, int count)
{
    uint32_t sum = 0;
    int i;

    for (i = 0; i < count; i++) {
        sum = (sum + pixels[i]) * 16777619u;
    }

    return sum;
}

int main(void)
{
    static uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    uint32_t sum;

    /*
     * Exercise an upstream function from common.c as well as the PPU. The
     * interrupt dispatcher is real upstream code and confirms that the archive
     * linked more than the renderer.
     */
    REG_IE = INTR_FLAG_VBLANK;
    REG_IF = INTR_FLAG_VBLANK;
    gIntrTable[INTR_INDEX_VBLANK] = CountVBlank;
    IntrMain();

    if (vblankCount != 1) {
        fprintf(stderr, "harness: upstream interrupt dispatch failed\n");
        return 1;
    }

    /* Forced blank is a deterministic PPU path: every pixel is 0xFFFF. */
    REG_DISPCNT = 0x0080;
    gpsp_draw_frame(framebuffer);

    sum = Checksum(framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    printf("harness: upstream software PPU %dx%d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    printf("harness: upstream interrupt dispatch ok\n");
    printf("harness: forced-blank checksum %08x\n", sum);
    printf("harness: first=%04x last=%04x\n", framebuffer[0], framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT - 1]);

    if (framebuffer[0] != 0xFFFF || framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT - 1] != 0xFFFF) {
        fprintf(stderr, "harness: unexpected framebuffer content\n");
        return 1;
    }

    return 0;
}
