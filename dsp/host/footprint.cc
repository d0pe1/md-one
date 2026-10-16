/* SPDX-License-Identifier: MIT
 *
 * RAM footprint of the Braids objects, against the FM-1's real budget.
 *
 * On this chip memory, not CPU, is what limits polyphony: MacroOscillator
 * embeds a DigitalOscillator whose physical-model delay lines dominate the
 * structure, and most of that is dead weight for the analog drone shapes.
 * This prints the numbers that decide how many voices the instrument can have.
 *
 * Host sizeof is a good guide but not authoritative for the target: pi32v2
 * alignment could differ. The firmware build reports the real .bss figure.
 */
#include "fm1_braids.h"

#include <stdio.h>

/* The FM-1 has 512 KiB of data RAM at 0x01C00000 (fm1-soc/src/lib.rs; the
 * regions in references/dinghy/firmware/app.ld span 0x01C00000..0x01C80000).
 * Two of those regions are ours, and they are separate budgets:
 *
 *   RAM  .data/.bss  96 KiB at 0x01C08000 -- ordinary state
 *   POOL .pool      336 KiB at 0x01C20000 -- big zeroed buffers: FX lines, canvases
 *
 * Dinghy's unmodified build reports "RAM .data+.bss 12368 B of 98304; pool
 * 30720 B of 344064", so it occupies ~42 KiB of the 512 and leaves ~390 KiB.
 *
 * Counting only the .bss region, as an earlier version of this file did, makes
 * voices look scarce when they are not: a voice array can be placed in .pool
 * with __attribute__((section(".pool"))), and FX lines live there regardless,
 * so voices and reverb do not compete. */
#define RAM_FREE  85936u    /* 98304 - 12368 */
#define POOL_FREE 313344u   /* 344064 - 30720 */

int main(void)
{
    const size_t analog  = sizeof(braids::AnalogOscillator);
    const size_t digital = sizeof(braids::DigitalOscillator);
    const size_t macro   = sizeof(braids::MacroOscillator);
    const size_t voice   = sizeof(fm1::BraidsVoice);

    puts("object sizes");
    printf("  %-28s %8zu B\n", "braids::AnalogOscillator",  analog);
    printf("  %-28s %8zu B\n", "braids::DigitalOscillator", digital);
    printf("  %-28s %8zu B\n", "braids::MacroOscillator",   macro);
    printf("  %-28s %8zu B\n", "fm1::BraidsVoice",          voice);
    printf("\n  of MacroOscillator, DigitalOscillator is %.0f%%\n",
           100.0 * (double)digital / (double)macro);

    printf("\nmemory available to the app (512 KiB total on the device)\n");
    printf("  %-22s %8u B free\n", "RAM  .data/.bss", RAM_FREE);
    printf("  %-22s %8u B free\n", "POOL .pool", POOL_FREE);
    printf("  %-22s %8u B free\n", "combined", RAM_FREE + POOL_FREE);

    printf("\nvoices of full MacroOscillator (every shape available per voice)\n");
    printf("  %-7s %14s   %-24s %s\n", "voices", "bytes", "if placed in .bss", "if placed in .pool");
    for (int v = 1; v <= 16; v++) {
        if (v > 8 && v % 4) continue;          /* thin out the tail */
        size_t full = (size_t)v * voice;
        printf("  %-7d %12zu B   %-24s %s\n", v, full,
               full > RAM_FREE  ? "over"
                   : full > RAM_FREE * 3 / 4 ? "tight" : "ok",
               full > POOL_FREE ? "over"
                   : full > POOL_FREE * 3 / 4 ? "tight" : "ok");
    }

    printf("\n  Four voices is already twelve oscillators: the TRIPLE_* shapes\n");
    printf("  are three detuned AnalogOscillators each. Memory is not the\n");
    printf("  limit here -- CPU and taste are. See docs/BRAIDS_PORT.md.\n");
    return 0;
}
