/* SPDX-License-Identifier: MIT
 *
 * What does a Braids voice cost, relative to the FM-1's audio budget?
 *
 * app_audio() gets 256 frames and 5.8 ms, and RULES.md advises staying under
 * half of that. The host is far faster than a 240 MHz pi32v2, so these are
 * RELATIVE figures: they rank shapes against each other and against our own
 * oscillator, and show how cost scales with voice count. They do not predict
 * the device. See docs/OPEN_QUESTIONS.md Q2.
 */
#define _POSIX_C_SOURCE 199309L

#include "../mi/fm1_braids.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define SR        44100u
#define BLOCK     256u
#define BUDGET_US 5805.0

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

struct Entry { const char *name; braids::MacroOscillatorShape shape; };

static const Entry kShapes[] = {
    { "triple_saw",     braids::MACRO_OSC_SHAPE_TRIPLE_SAW     },
    { "triple_sine",    braids::MACRO_OSC_SHAPE_TRIPLE_SINE    },
    { "saw_swarm",      braids::MACRO_OSC_SHAPE_SAW_SWARM      },
    { "harmonics",      braids::MACRO_OSC_SHAPE_HARMONICS      },
    { "bowed",          braids::MACRO_OSC_SHAPE_BOWED          },
    { "fluted",         braids::MACRO_OSC_SHAPE_FLUTED         },
    { "vowel_fof",      braids::MACRO_OSC_SHAPE_VOWEL_FOF      },
    { "filtered_noise", braids::MACRO_OSC_SHAPE_FILTERED_NOISE },
    { "wavetables",     braids::MACRO_OSC_SHAPE_WAVETABLES     },
};

static double bench_one(braids::MacroOscillatorShape shape, int voices, int iters)
{
    static fm1::BraidsVoice v[8];
    static int16_t tmp[BLOCK];
    static int32_t mix[BLOCK];

    for (int i = 0; i < voices; i++) {
        v[i].Init();
        v[i].set_shape(shape);
        v[i].set_pitch(fm1::pitch_from_midi(36 + 7 * i));
        v[i].set_parameters(16384, 16384);
    }
    for (int i = 0; i < voices; i++) v[i].Render(tmp, BLOCK);   /* warm */

    double t0 = now_us();
    for (int it = 0; it < iters; it++) {
        memset(mix, 0, sizeof mix);
        for (int i = 0; i < voices; i++) {
            v[i].Render(tmp, BLOCK);
            for (uint32_t s = 0; s < BLOCK; s++) mix[s] += tmp[s];
        }
    }
    double t1 = now_us();
    if (mix[0] == 0x7FFFFFFF) fputs("", stderr);   /* keep the mix alive */
    return (t1 - t0) / iters;
}

int main(void)
{
    printf("Braids voices against the FM-1 audio block (%u frames, %.2f ms)\n",
           BLOCK, BUDGET_US / 1000.0);
    printf("HOST timings -- relative only, not a prediction for 240 MHz pi32v2\n\n");
    printf("  %-16s %10s %10s %10s\n", "shape", "1 voice", "4 voices", "x4 scale");
    printf("  %-16s %10s %10s %10s\n", "----------------", "--------", "--------", "--------");

    for (size_t i = 0; i < sizeof kShapes / sizeof *kShapes; i++) {
        double one  = bench_one(kShapes[i].shape, 1, 4000);
        double four = bench_one(kShapes[i].shape, 4, 2000);
        printf("  %-16s %8.2fus %8.2fus %9.2fx\n",
               kShapes[i].name, one, four, four / one);
    }

    printf("\n  for scale, our own Q15 sine (dsp/shared) was 0.22 us/block/voice\n");
    printf("  footprint: %zu B per BraidsVoice\n", sizeof(fm1::BraidsVoice));
    return 0;
}
