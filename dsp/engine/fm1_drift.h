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
#include "../shared/fm1_lfo_table.h"

typedef struct {
    uint32_t rng;
    uint32_t phase;     /* wraps once per drift cycle; the rate IS a rate */
    int32_t  value;     /* current, Q15 bipolar */
    int32_t  target;    /* where it is heading, Q15 bipolar */
} fm1_drift_t;

static inline void fm1_drift_init(fm1_drift_t *d, uint32_t seed)
{
    d->rng = seed ? seed : 0x1234567u;
    d->phase = 0;
    d->value = 0;
    d->target = 0;
}

/* Advance one block. rate 0..127 sets how often a new target is chosen;
 * depth 0..127 scales the result. Returns the current value in Q15, bipolar. */
static inline int32_t fm1_drift_block(fm1_drift_t *d, uint8_t rate, uint8_t depth)
{
    uint32_t inc = fm1_lfo_inc[rate & 127u];
    uint32_t prev = d->phase;
    int32_t  k;

    if (depth == 0) {
        /* Still decay toward zero so turning depth down settles rather than
         * freezing the drift wherever it happened to be. */
        d->value -= d->value >> 6;
        return 0;
    }

    /* A PHASE, from the same table the LFOs use, so the knob is a rate in
     * hertz and both say the same thing by the same scale.
     *
     * It was `span = 2048 >> (rate >> 4)`, which is eight values across a
     * 0..127 control -- fifteen of every sixteen positions did nothing. That
     * is exactly the defect GLIDE was found to have and fixed for, and this
     * one survived because the knob said 0..127 and nobody could tell what
     * it was supposed to be doing. */
    d->phase += inc;
    if (d->phase < prev) {              /* wrapped: pick somewhere new */
        d->rng = d->rng * 1664525u + 1013904223u;
        d->target = (int32_t)((d->rng >> 16) & 0xFFFFu) - 32768;
    }

    /* Glide toward it at a rate proportional to the cycle, so the movement
     * stays continuous instead of stepping -- a fast drift that crawled
     * between targets would be a slow drift with a stutter. Also was a
     * shift, with four values. */
    k = (int32_t)(inc >> 16);
    if (k < 1) k = 1;
    d->value += (int32_t)(((int64_t)(d->target - d->value) * k) >> 16);

    return (d->value * (int32_t)depth) >> 7;
}

#endif /* FM1_DRIFT_H */
