/* SPDX-License-Identifier: MIT
 *
 * A tuned feedback comb -- the drone engine's resonator.
 *
 * This is where the instrument's character comes from. Reverb was deliberately
 * left out (the output goes straight into a DAW, which has better reverb for
 * free); the pool buys tuned resonance instead, which imposes pitch and
 * structure rather than smearing everything into an average.
 *
 * Integer only, per the chip's rules. The one subtlety is computing the delay
 * length without a 64-bit divide:
 *
 *   delay_samples = samplerate / frequency
 *
 * and the oscillator already carries inc = 2^32 * f / fs, so
 *
 *   delay = 2^32 / inc
 *
 * 2^32 does not fit in 32 bits, but 0xFFFFFFFF / inc differs from it by less
 * than one part in 4 billion. The fractional part comes from a second 32-bit
 * division of the remainder, giving 1/256-sample resolution -- enough that
 * comb tuning error stays well under a cent at drone frequencies.
 */
#ifndef FM1_COMB_H
#define FM1_COMB_H

#include "../shared/fm1_fixed.h"

/* 2048 samples at 44.1 kHz = 46 ms, so the comb tunes down to ~21.5 Hz.
 * A power of two so the wrap is a mask. 4 KB per voice, in .pool on target. */
#define FM1_COMB_LEN  2048u
#define FM1_COMB_MASK (FM1_COMB_LEN - 1u)

typedef struct {
    int16_t  buf[FM1_COMB_LEN];
    uint32_t w;          /* write cursor */
    uint32_t delay_q8;   /* delay in samples, 24.8 */
    int32_t  fb;         /* feedback, Q15; |fb| < 32768 or it runs away */
    int32_t  in_gain;    /* 32768 - |fb|, see fm1_comb_set_feedback */
} fm1_comb_t;

static inline void fm1_comb_init(fm1_comb_t *c)
{
    for (uint32_t i = 0; i < FM1_COMB_LEN; i++) c->buf[i] = 0;
    c->w = 0;
    c->delay_q8 = 256u * 100u;     /* a harmless default */
    c->fb = 0;
    c->in_gain = 32768;            /* unity when there is no feedback */
}

/* Tune the comb from an oscillator phase increment (see fm1_fixed.h).
 * ratio_q12 scales the delay: 4096 = the note itself, 8192 = an octave down,
 * 2048 = an octave up. */
static inline void fm1_comb_set_from_inc(fm1_comb_t *c, uint32_t inc, int32_t ratio_q12)
{
    if (inc == 0) return;

    uint32_t whole = 0xFFFFFFFFu / inc;             /* ~ 2^32 / inc */
    uint32_t rem   = 0xFFFFFFFFu % inc;
    /* rem < inc, and inc is at most a few million at audio rates, so
     * rem << 8 cannot overflow 32 bits. */
    uint32_t frac  = (rem << 8) / inc;

    uint32_t d = (whole << 8) | (frac & 0xFFu);
    /* scale by the ratio in Q12; 64-bit multiply is allowed, division is not */
    d = (uint32_t)(((uint64_t)d * (uint64_t)(uint32_t)ratio_q12) >> 12);

    /* Leave room for the interpolation tap and the one-sample write lead. */
    if (d < (2u << 8))                      d = 2u << 8;
    if (d > ((FM1_COMB_LEN - 2u) << 8))     d = (FM1_COMB_LEN - 2u) << 8;
    c->delay_q8 = d;
}

static inline void fm1_comb_set_feedback(fm1_comb_t *c, int32_t fb_q15)
{
    /* Hard-limit below unity: at exactly 1.0 the comb is an oscillator and
     * never decays, which on a sustained drone means it grows without bound. */
    if (fb_q15 >  31000) fb_q15 =  31000;
    if (fb_q15 < -31000) fb_q15 = -31000;
    c->fb = fb_q15;

    /* Compensate the input for the resonant gain.
     *
     * A feedback comb amplifies sustained input at its resonant frequency by
     * 1/(1-fb) -- about 4x at fb=0.75, 20x at fb=0.95. The loop saturates
     * internally long before that, BEFORE any output gain, so it never shows
     * as clipping in the rendered file; it just sounds harsh. Measured at
     * fb=0.73 with sustained half-scale input: 7600 of 8000 samples pinned at
     * full scale. On a struck bell that is a "grizzled" attack carried into
     * the tail.
     *
     * Scaling the input by (1-fb) normalises the resonant peak back to about
     * unity. The side effect is correct and desirable: a high-feedback comb
     * now rejects off-resonance content instead of passing everything loudly,
     * so it reads as a resonator rather than a wall.
     *
     * dsp/tests/test_comb.c holds this invariant. */
    int32_t mag = fb_q15 < 0 ? -fb_q15 : fb_q15;
    c->in_gain = 32768 - mag;
}

/* One sample. Returns the comb's output; the caller blends it with the dry
 * signal. */
static inline q15_t fm1_comb_tick(fm1_comb_t *c, q15_t x)
{
    uint32_t di = c->delay_q8 >> 8;
    uint32_t df = c->delay_q8 & 0xFFu;

    uint32_t r0 = (c->w - di) & FM1_COMB_MASK;
    uint32_t r1 = (r0 - 1u) & FM1_COMB_MASK;

    int32_t a = c->buf[r0];
    int32_t b = c->buf[r1];
    int32_t y = a + (((b - a) * (int32_t)df) >> 8);     /* linear interpolation */

    int32_t v = (((int32_t)x * c->in_gain) >> Q15_BITS) + ((y * c->fb) >> Q15_BITS);
    q15_t   s = fm1_q31_to_q15_sat(v);

    c->buf[c->w] = s;
    c->w = (c->w + 1u) & FM1_COMB_MASK;
    return s;
}

#endif /* FM1_COMB_H */
