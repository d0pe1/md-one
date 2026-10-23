/* SPDX-License-Identifier: MIT
 *
 * A trapezoidal (TPT / zero-delay-feedback) state-variable filter, integer.
 *
 * This exists because the Chamberlin SVF we shipped cannot reach the top of
 * its own knob. Measured on the host at RES 0, LOWPASS, with the panel
 * reading 8372 Hz at CUT 120:
 *
 *     slope      knob says     actually
 *     6 dB        8372 Hz      15116 Hz
 *     12 dB       8372 Hz       2974 Hz
 *     24 dB       8372 Hz       1909 Hz
 *
 * -- and above CUT 115 the output stopped changing at all, so the last
 * twelve steps of the knob did nothing. Three slopes, three different
 * answers, none of them the displayed one. The cause is structural and
 * fm1_svf_table.h already documented it: a Chamberlin SVF is only well
 * behaved below about fs/6, so the tuning table clamps at note 114 and the
 * filter rings instead of opening above it.
 *
 * The trapezoidal form is the bilinear transform of the analogue prototype.
 * Its frequency warping is exactly tan(pi fc / fs), so the coefficient grows
 * without bound towards Nyquist rather than the filter going unstable, and
 * the cutoff it is tuned to is the cutoff you get. At note 127 and 44.1 kHz
 * we are at 0.284 fs, which is not close to the edge.
 *
 * The algorithm is Andrew Simper's (Cytomic, "Solving the continuous SVF
 * equations using trapezoidal integration"), a published derivation that has
 * been reimplemented many times. This implementation is ours, written from
 * that published form. It deliberately shares no code with the GPL firmwares
 * in references/ that also use it: dsp/ is MIT and stays GPL-free
 * (docs/LICENSE_REGISTER.md).
 *
 * Q24 throughout for the coefficients, and that is forced rather than
 * chosen: a3 is g^2 * a1, and at the bottom of the range g is 0.00058, so at
 * Q12 or Q13 a3 rounds to zero and the filter stops integrating -- it holds
 * its last value for ever and becomes a latch rather than a filter.
 */
#ifndef FM1_TPT_H
#define FM1_TPT_H

#include "../shared/fm1_fixed.h"
#include "../shared/fm1_tpt_table.h"

/* The signal runs 3 bits hot inside the filter. A resonant peak rings well
 * above its input, and the integrator states carry the ringing, so the extra
 * bits are what keep a quiet input from quantising to nothing on the way
 * round the loop. Taken back out at the output. */
#define FM1_TPT_IN    3
#define FM1_TPT_SMAX  (1 << 28)     /* state clamp: 64-bit products stay exact */

typedef struct {
    int32_t a1, a2, a3;     /* Q24, solved once per block */
    int32_t k;              /* damping, Q12: 1/Q */
    int32_t pitch;          /* the tuning the coefficients were solved from */
    int32_t s1, s2;         /* the two integrators */
} fm1_tpt_t;

static inline void fm1_tpt_clear(fm1_tpt_t *t) { t->s1 = t->s2 = 0; }

/* (1 << 48) / den, for den >= (1 << 24).
 *
 * Our own long division. The firmware links no compiler runtime and
 * dsp/target/check.sh bans __udivdi3 by name, so the alternative to writing
 * this is not a slower divide, it is a link error.
 *
 * A bit-by-bit division of 1 << 48 spends its first twenty-four steps merely
 * doubling the remainder -- den is at least 1 << 24, so no quotient bit can
 * be set before then -- so those steps are skipped by starting at 1 << 24.
 * Twenty-five iterations remain, run once per filter per block.
 */
static inline int32_t fm1_tpt_recip(uint32_t den)
{
    uint32_t q = 0, r = 1u << 24;
    int i;

    for (i = 24; i >= 0; i--) {
        if (r >= den) { r -= den; q |= 1u << i; }
        r <<= 1;                /* r < den here, and den stays below 1 << 27 */
    }
    return (int32_t)q;
}

/* tan(pi fc / fs) for a cutoff in 1/128 semitone, Q24, interpolated within
 * the semitone so a modulated sweep is smooth rather than stepping. */
static inline int32_t fm1_tpt_g(int32_t pitch)
{
    int32_t i, frac, g;

    if (pitch < 0)         pitch = 0;
    if (pitch > 127 * 128) pitch = 127 * 128;
    i    = pitch >> 7;
    frac = pitch & 127;
    g    = fm1_tpt_g24[i];
    if (frac)
        g += (int32_t)((((int64_t)fm1_tpt_g24[i + 1] - g) * frac) >> 7);
    return g;
}

/* Solve the loop for the tuning currently held.
 *
 * BOTH setters below end here, and that is the point: a1, a2 and a3 depend
 * on the cutoff AND on the damping, so a set_res that only stored k would
 * leave the coefficients describing the old resonance until something else
 * happened to retune. That is exactly how it was first written, and
 * test_mix's clipping case caught it immediately -- two bandpasses at RES
 * 127 in series could not drive a channel to full scale, because neither of
 * them had ever been told.
 *
 * Everything here is block rate. The sample loop below does nothing but
 * multiply and add. */
static inline void fm1_tpt_solve(fm1_tpt_t *t)
{
    int32_t g = fm1_tpt_g(t->pitch);
    uint32_t den;

    /* den = (1 + g*(g + k)) in Q24. k is Q12, so it is lifted to Q24 to meet
     * g; the product is Q48 and comes back down. */
    den = (uint32_t)((1 << 24) +
                     (int32_t)(((int64_t)g * ((int64_t)g + ((int64_t)t->k << 12))) >> 24));
    t->a1 = fm1_tpt_recip(den);
    t->a2 = (int32_t)(((int64_t)t->a1 * g) >> 24);
    t->a3 = (int32_t)(((int64_t)t->a2 * g) >> 24);
}

/* RESONANCE -> damping. k is 1/Q, so it runs the other way from the knob.
 *
 * The bottom is BUTTERWORTH, k = sqrt(2), Q = 0.707 -- not the k = 2.0,
 * Q = 0.5 it started at. That matters for a reason beyond taste: a
 * Butterworth two-pole is maximally flat and its -3 dB point lands exactly
 * on the cutoff it is tuned to, so at RES 0 the 12 dB slope now agrees with
 * the 6 dB one instead of cornering a third of an octave lower. Q = 0.5 is
 * over-damped; it reads as a filter that is slightly shut even when the
 * resonance knob is all the way down.
 *
 * The top stops at 0.025 (Q = 40) rather than 0: at k = 0 the loss term
 * vanishes and the filter stops being a filter and becomes a sine
 * oscillator. */
static inline void fm1_tpt_set_res(fm1_tpt_t *t, uint8_t res)
{
    if (res > 127) res = 127;
    t->k = 5793 - ((int32_t)res * 5691) / 127;      /* Q12: 1.414 .. 0.025 */
    fm1_tpt_solve(t);
}

/* CUTOFF, in 1/128 semitone -- the unit the pitch, the comb and the key all
 * speak, so a modulated sweep is smooth rather than stepping a semitone at a
 * time. */
static inline void fm1_tpt_set_pitch(fm1_tpt_t *t, int32_t pitch)
{
    if (pitch < 0)         pitch = 0;
    if (pitch > 127 * 128) pitch = 127 * 128;
    t->pitch = pitch;
    fm1_tpt_solve(t);
}

static inline void fm1_tpt_init(fm1_tpt_t *t)
{
    t->s1 = t->s2 = 0;
    t->k  = 8192;                       /* Q = 0.5, no peak */
    fm1_tpt_set_pitch(t, 64 * 128);     /* a defined corner, not zeroed coefficients */
}

/* One sample. `in` is Q15; the three outputs come back Q15.
 *
 * The trapezoidal update solves the zero-delay feedback loop directly rather
 * than iterating it, which is the whole point of the form: no unit delay in
 * the feedback path, so the tuning does not drift with frequency and the
 * filter stays stable where a Chamberlin falls apart. */
typedef struct { int32_t lp, bp, hp; } fm1_tpt_out_t;

static inline fm1_tpt_out_t fm1_tpt_tick(fm1_tpt_t *t, int32_t in)
{
    fm1_tpt_out_t o;
    int32_t x  = in << FM1_TPT_IN;
    int32_t v3 = x - t->s2;
    int32_t v1 = (int32_t)(((int64_t)t->a1 * t->s1 +
                            (int64_t)t->a2 * v3 + (1 << 23)) >> 24);
    int32_t v2 = t->s2 + (int32_t)(((int64_t)t->a2 * t->s1 +
                                    (int64_t)t->a3 * v3 + (1 << 23)) >> 24);
    int32_t s1 = 2 * v1 - t->s1;
    int32_t s2 = 2 * v2 - t->s2;

    t->s1 = s1 > FM1_TPT_SMAX ? FM1_TPT_SMAX : (s1 < -FM1_TPT_SMAX ? -FM1_TPT_SMAX : s1);
    t->s2 = s2 > FM1_TPT_SMAX ? FM1_TPT_SMAX : (s2 < -FM1_TPT_SMAX ? -FM1_TPT_SMAX : s2);

    /* Rounded on the way out, not floored: a floor holds a DC offset of half
     * a bit for ever, and this project has paid for that mistake five times
     * in other files. */
    o.lp = (v2 + (1 << (FM1_TPT_IN - 1))) >> FM1_TPT_IN;
    o.bp = (v1 + (1 << (FM1_TPT_IN - 1))) >> FM1_TPT_IN;
    o.hp = ((x - (int32_t)(((int64_t)t->k * v1) >> 12) - v2)
            + (1 << (FM1_TPT_IN - 1))) >> FM1_TPT_IN;
    return o;
}

/* --- one pole, 6 dB per octave ------------------------------------------
 *
 * The same bilinear transform, one integrator instead of two. It is here
 * because the 6 dB slope was the last thing in the instrument still tuned
 * from the CHAMBERLIN table, and it did not agree with the other two: at
 * CUT 120 it measured a real 15116 Hz where 12 dB managed 2974 and 24 dB
 * 1909. Three slopes, three different filters. Changing SLOPE moved the
 * corner by an octave and a half as a side effect, which is not what that
 * knob is for.
 *
 * The coefficient is g / (1 + g), and it is derived from the reciprocal
 * already written rather than from a second divide: fm1_tpt_recip(den)
 * returns 2^48 / den, so with den = (1 + g) in Q24 that is Q24 of 1/(1 + g),
 * and g/(1+g) is one minus it.
 */
typedef struct {
    int32_t g1;             /* g / (1 + g), Q24 */
    int32_t s;              /* the integrator, 3 bits hot like the SVF */
} fm1_tpt1_t;

static inline void fm1_tpt1_set_pitch(fm1_tpt1_t *t, int32_t pitch)
{
    int32_t g = fm1_tpt_g(pitch);
    t->g1 = (1 << 24) - fm1_tpt_recip((uint32_t)((1 << 24) + g));
}

static inline void fm1_tpt1_clear(fm1_tpt1_t *t) { t->s = 0; }

static inline void fm1_tpt1_init(fm1_tpt1_t *t)
{
    t->s = 0;
    fm1_tpt1_set_pitch(t, 64 * 128);
}

/* Returns the LOW pass; the high pass is the input minus it, which is exact
 * here in a way it is not for a Chamberlin. */
static inline int32_t fm1_tpt1_lp(fm1_tpt1_t *t, int32_t in)
{
    int32_t x = in << FM1_TPT_IN;
    int32_t v = (int32_t)(((int64_t)(x - t->s) * t->g1 + (1 << 23)) >> 24);
    int32_t y = v + t->s;
    int32_t s = y + v;

    t->s = s > FM1_TPT_SMAX ? FM1_TPT_SMAX : (s < -FM1_TPT_SMAX ? -FM1_TPT_SMAX : s);
    return (y + (1 << (FM1_TPT_IN - 1))) >> FM1_TPT_IN;
}

#endif /* FM1_TPT_H */
