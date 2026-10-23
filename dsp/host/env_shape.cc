/* SPDX-License-Identifier: MIT
 *
 * Do the envelope modes actually differ, and by how much?
 *
 * ANALOG and DIGITAL are meant to take the SAME time and arrive by different
 * routes: an exponential approach against a straight line. That is easy to
 * claim and easy to get wrong -- a linear step derived from the wrong
 * constant lands in half the time or twice it -- so this prints the fall
 * itself rather than asserting on a duration.
 */
#include "../engine/fm1_env.h"

#include <stdio.h>

/* The curve knob, sampled across its travel: the same release, shaped. */
static void curve_sweep(void)
{
    static const uint8_t CV[5] = { 0, 32, 64, 96, 127 };
    printf("\nRELEASE 70, RELEASE CURVE swept. Time at each tenth down.\n\n");
    for (int k = 0; k < 5; k++) {
        fm1_env_t e;
        long n = 0;
        int next = 9;

        fm1_env_init(&e);
        fm1_env_set_mode(&e, ENV_M_ANALOG);
        fm1_env_set_curve_r(&e, CV[k]);
        fm1_env_set_release(&e, 70);
        fm1_env_set_sustain(&e, 127);
        fm1_env_gate(&e, 1);
        e.level = FM1_ENV_ONE;
        e.stage = ENV_SUSTAIN;
        fm1_env_gate(&e, 0);

        printf("    curve %3d  ", CV[k]);
        while (e.stage != ENV_IDLE && n < 44100L * 60) {
            fm1_env_tick(&e);
            n++;
            while (next >= 0 &&
                   e.level <= (int32_t)((int64_t)FM1_ENV_ONE * next / 10)) {
                printf("%5.2f ", (double)n / 44100.0);
                next--;
            }
        }
        printf("\n");
    }
}

int main(void)
{
    static const uint8_t modes[2] = { ENV_M_ANALOG, ENV_M_DIGITAL };
    static const char *name[2] = { "ANALOG ", "DIGITAL" };

    printf("\nRELEASE from full, time in seconds at each tenth of the way down.\n");
    printf("Both modes should take about the same TIME by different SHAPES.\n\n");

    for (int rel = 40; rel <= 100; rel += 30) {
        printf("  RELEASE %d\n", rel);
        for (int m = 0; m < 2; m++) {
            fm1_env_t e;
            long n = 0;
            int next = 9;

            fm1_env_init(&e);
            fm1_env_set_mode(&e, modes[m]);
            fm1_env_set_release(&e, (uint8_t)rel);
            fm1_env_set_sustain(&e, 127);
            fm1_env_gate(&e, 1);
            e.level = FM1_ENV_ONE;
            e.stage = ENV_SUSTAIN;
            fm1_env_gate(&e, 0);

            printf("    %s  ", name[m]);
            while (e.stage != ENV_IDLE && n < 44100L * 60) {
                fm1_env_tick(&e);
                n++;
                while (next >= 0 &&
                       e.level <= (int32_t)((int64_t)FM1_ENV_ONE * next / 10)) {
                    printf("%5.2f ", (double)n / 44100.0);
                    next--;
                }
            }
            printf(" total %.2f s\n", (double)n / 44100.0);
        }
    }
    curve_sweep();
    return 0;
}
