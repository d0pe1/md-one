/* SPDX-License-Identifier: MIT
 *
 * Measure the shared oscillator against the firmware's real audio budget.
 *
 * app_audio() gets 256 frames and must finish well inside 5.8 ms; RULES.md
 * advises staying under half of that. The host is far faster than a 240 MHz
 * pi32v2, so this does NOT predict target cost -- it gives a relative number
 * to track as the engine grows, and a per-voice figure to reason with.
 * The authoritative target measurement is fm1_ticks() on the device or a cycle
 * count from the emulator; see docs/OPEN_QUESTIONS.md.
 */
/* clock_gettime and struct timespec are POSIX, not C99; -std=c99 hides them
 * unless we ask for the POSIX.1b realtime extensions explicitly. Must precede
 * every include. */
#define _POSIX_C_SOURCE 199309L

#include "../shared/fm1_osc.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SR         44100u
#define BLOCK      256u
#define BUDGET_US  5805.0          /* 256 / 44100 s */
#define VOICES_MAX 16

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

int main(void)
{
    static int16_t mix[BLOCK];
    static int16_t tmp[BLOCK];
    fm1_osc_t v[VOICES_MAX];

    printf("block %u frames @ %u Hz -> %.2f ms of audio per block\n",
           BLOCK, SR, BUDGET_US / 1000.0);
    printf("host is not the target: these are relative figures only\n\n");
    printf("  voices   us/block   %% of budget   blocks/s\n");

    for (int nv = 1; nv <= VOICES_MAX; nv <<= 1) {
        for (int i = 0; i < nv; i++) {
            v[i].phase = (uint32_t)i * 0x10000000u;
            fm1_osc_set_freq(&v[i], fm1_hz(110u + 37u * (uint32_t)i), FM1_K_44100);
        }

        const int iters = 20000;
        /* warm the caches so the first iteration does not dominate */
        for (int i = 0; i < nv; i++) fm1_osc_sine_block(&v[i], tmp, BLOCK);

        double t0 = now_us();
        for (int it = 0; it < iters; it++) {
            for (uint32_t s = 0; s < BLOCK; s++) mix[s] = 0;
            for (int i = 0; i < nv; i++) {
                fm1_osc_sine_block(&v[i], tmp, BLOCK);
                for (uint32_t s = 0; s < BLOCK; s++)
                    mix[s] = fm1_q15_add_sat(mix[s], (q15_t)(tmp[s] >> 2));
            }
        }
        double t1 = now_us();

        double per = (t1 - t0) / iters;
        printf("  %6d   %8.2f   %10.2f   %8.0f\n",
               nv, per, 100.0 * per / BUDGET_US, 1e6 / per);

        /* keep the optimiser from discarding the mix */
        if (mix[0] == 0x7FFF && mix[1] == 0x7FFF) fputs("", stderr);
    }

    printf("\nstatic footprint: osc %zu B/voice, block buffer %zu B\n",
           sizeof(fm1_osc_t), sizeof mix);
    return 0;
}
