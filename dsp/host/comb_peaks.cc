/* SPDX-License-Identifier: MIT
 *
 * Does the sign of the comb's feedback change WHICH harmonics peak?
 *
 * Positive feedback resonates at every multiple of 1/delay. Negative
 * inverts each pass, so even multiples cancel and only the odd ones peak.
 * That is the claim; this measures it by sweeping a sine through the comb
 * and reporting the gain at each harmonic of its tuning.
 */
#include "../engine/fm1_comb.h"
#include <stdio.h>
#include <math.h>

#define SR 44100.0

static double gain_at(int32_t fb, double delay_samples, double hz)
{
    fm1_comb_t c;
    double ph = 0.0, step = 2.0 * M_PI * hz / SR;
    int32_t pk = 0;

    fm1_comb_init(&c);
    c.delay_q8 = (uint32_t)(delay_samples * 256.0);
    fm1_comb_set_feedback(&c, fb);

    for (int i = 0; i < 60000; i++) {
        int32_t x = (int32_t)(8000.0 * sin(ph));
        ph += step;
        int32_t y = fm1_comb_tick(&c, (q15_t)x);
        if (i > 40000) { int32_t a = y < 0 ? -y : y; if (a > pk) pk = a; }
    }
    return (double)pk / 8000.0;
}

int main(void)
{
    const double D = 100.0;            /* 441 Hz fundamental */
    const double f0 = SR / D;

    printf("\ncomb tuned to %.0f Hz, gain at each harmonic\n\n", f0);
    printf("  %-10s %12s %12s\n", "HARMONIC", "FB +0.9", "FB -0.9");
    printf("  %-10s %12s %12s\n", "----------", "------------", "------------");
    for (int n = 1; n <= 6; n++) {
        double hz = f0 * n;
        printf("  %-3d %6.0f Hz %11.2fx %11.2fx\n",
               n, hz, gain_at(29491, D, hz), gain_at(-29491, D, hz));
    }
    printf("\n  and halfway between, where a negative comb peaks instead:\n");
    for (int n = 1; n <= 3; n++) {
        double hz = f0 * (n - 0.5);
        printf("  %-3.1f %6.0f Hz %11.2fx %11.2fx\n",
               n - 0.5, hz, gain_at(29491, D, hz), gain_at(-29491, D, hz));
    }
    return 0;
}
