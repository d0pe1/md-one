/* SPDX-License-Identifier: MIT
 *
 * Slow random drift -- what turns a sustained note into a drone.
 *
 * A random walk between held targets, slewed, updated once per audio block
 * rather than per sample. At 44.1 kHz a block is 5.8 ms, which is far faster
 * than anything audible as movement here: full-scale travel takes seconds to
 * minutes. Per-sample updating would cost 256x more for no perceptible gain.
 *
 * Deterministic from its seed, so a patch sounds the same twice and the tests
 * can assert on it.
 */
#ifndef FM1_DRIFT_H
#define FM1_DRIFT_H

#include "../shared/fm1_fixed.h"

typedef struct {
    uint32_t rng;
    int32_t  value;     /* current, Q15 bipolar */
    int32_t  target;    /* where it is heading, Q15 bipolar */
    uint32_t countdown; /* blocks until a new target is chosen */
} fm1_drift_t;

static inline void fm1_drift_init(fm1_drift_t *d, uint32_t seed)
{
    d->rng = seed ? seed : 0x1234567u;
    d->value = 0;
    d->target = 0;
    d->countdown = 1;
}

/* Advance one block. rate 0..127 sets how often a new target is chosen;
 * depth 0..127 scales the result. Returns the current value in Q15, bipolar. */
static inline int32_t fm1_drift_block(fm1_drift_t *d, uint8_t rate, uint8_t depth)
{
    if (depth == 0) {
        /* Still decay toward zero so turning depth down settles rather than
         * freezing the drift wherever it happened to be. */
        d->value -= d->value >> 6;
        return 0;
    }

    if (--d->countdown == 0) {
        d->rng = d->rng * 1664525u + 1013904223u;
        /* A new target anywhere in range. */
        d->target = (int32_t)((d->rng >> 16) & 0xFFFF) - 32768;
        /* rate 0 -> ~2048 blocks (12 s) between targets, 127 -> ~16 (93 ms) */
        uint32_t span = 2048u >> (rate >> 4);
        if (span < 16u) span = 16u;
        d->countdown = span;
    }

    /* One-pole slew toward the target. The shift sets the glide; faster rates
     * glide faster so the movement stays continuous rather than stepped. */
    uint32_t sh = 9u - (uint32_t)(rate >> 5);      /* 9..5 */
    d->value += (d->target - d->value) >> sh;

    return (d->value * (int32_t)depth) >> 7;
}

#endif /* FM1_DRIFT_H */
