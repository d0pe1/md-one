/* SPDX-License-Identifier: MIT
 *
 * Fixed-point primitives for FM-1 DSP code.
 *
 * Everything in dsp/shared/ compiles for BOTH the host and the pi32v2 target,
 * so it obeys the target's rules (references/dinghy/docs/RULES.md):
 *
 *   - no float and no double: the AC791N has no FPU and the toolchain ships no
 *     libgcc soft-float, so a single float operation fails the link;
 *   - no 64-bit division or modulo: __udivdi3 does not exist. 64-bit
 *     MULTIPLICATION is fine and is used freely below;
 *   - no constant may land in 0xFFC00000..0xFFCFFFFF (the chip ROM), because
 *     the firmware build rejects it as a possible ROM call.
 *
 * Conventions, matching the rest of the FM-1 community code:
 *   Q15  levels and audio samples   (1.0 == 32767)
 *   Q12  gains                      (1.0 == 4096)
 *   Q16  frequencies in Hz          (440 Hz == 440 << 16)
 *   u32  oscillator phase, the full 32 bits spanning one cycle
 */
#ifndef FM1_FIXED_H
#define FM1_FIXED_H

#include <stdint.h>

typedef int16_t  q15_t;   /* audio sample / level */
typedef int32_t  q31_t;   /* accumulator */
typedef uint32_t q16_t;   /* unsigned 16.16, used for frequency in Hz */

#define Q15_BITS  15
#define Q15_ONE   32767
#define Q15_MIN  (-32768)

#define Q12_BITS  12
#define Q12_ONE   4096

/* a * b in Q15. The product of two int16 fits int32, so no 64-bit needed. */
static inline q15_t fm1_q15_mul(q15_t a, q15_t b)
{
    return (q15_t)(((int32_t)a * (int32_t)b) >> Q15_BITS);
}

/* a * g where g is a Q12 gain. */
static inline q15_t fm1_q12_scale(q15_t a, int32_t g_q12)
{
    return (q15_t)(((int32_t)a * g_q12) >> Q12_BITS);
}

/* Saturating add: FM and additive synthesis overflow constantly, and wrapping
 * turns a loud note into broadband noise rather than a clipped one. */
static inline q15_t fm1_q15_add_sat(q15_t a, q15_t b)
{
    int32_t s = (int32_t)a + (int32_t)b;
    if (s > Q15_ONE) return Q15_ONE;
    if (s < Q15_MIN) return Q15_MIN;
    return (q15_t)s;
}

static inline q15_t fm1_q31_to_q15_sat(q31_t s)
{
    if (s > Q15_ONE) return Q15_ONE;
    if (s < Q15_MIN) return Q15_MIN;
    return (q15_t)s;
}

/* --- soft clip -------------------------------------------------------------
 *
 * The mix bus carries two bits of headroom (docs/SIGNAL_PATH.md section 4), so
 * four correlated voices at full level reach about 4.0 before anything limits
 * them. This is where that comes back to 1.0, and it is the ONLY place the
 * instrument is allowed to distort.
 *
 * Hard clipping four detuned drone voices -- which sit near full scale by
 * nature -- produces broadband splatter. A knee is a gentler failure.
 *
 * The curve is one quadratic segment, in normalised terms:
 *
 *     |x| <= 0.5          y = x                      (untouched)
 *     0.5 < |x| < 1.5     y = x - (|x| - 0.5)^2 / 2  (knee)
 *     |x| >= 1.5          y = 1                      (hard)
 *
 * Continuous in value and in slope at both joins: the slope is 1.0 at 0.5 and
 * 0.0 at 1.5, so nothing kinks on the way in or out. One compare, one multiply
 * and a shift -- cheap enough for every output sample.
 *
 * In Q15 the divide by 2 and the 1/32768 normalisation fold into a single
 * >>16, which is why the knee sits at 0.5 rather than somewhere prettier.
 */
#define FM1_CLIP_KNEE  16384    /* 0.5 in Q15 */
#define FM1_CLIP_HARD  49152    /* 1.5 in Q15 -- the knee reaches 1.0 here */

static inline q15_t fm1_soft_clip(q31_t x)
{
    int32_t a = x < 0 ? -x : x;

    if (a <= FM1_CLIP_KNEE) return (q15_t)x;

    /* -Q15_ONE, not Q15_MIN. Two's complement gives one extra code below zero,
     * and spending it here would make the limiter asymmetric by 1 LSB at the
     * rail -- which is a DC offset and an even-harmonic generator on exactly
     * the fully-driven material this is meant to protect. One code of range is
     * the cheaper thing to give up. */
    if (a >= FM1_CLIP_HARD) return (q15_t)(x < 0 ? -Q15_ONE : Q15_ONE);

    int32_t d = a - FM1_CLIP_KNEE;      /* 0..32767, so d*d stays inside int32 */
    int32_t y = a - ((d * d) >> 16);
    if (y > Q15_ONE) y = Q15_ONE;       /* only reachable at the very top */

    return (q15_t)(x < 0 ? -y : y);
}

/* --- phase increment -------------------------------------------------------
 *
 * inc = freq / samplerate * 2^32, with no 64-bit division.
 *
 * Instead of dividing at run time we carry a per-sample-rate constant
 *     K = round(2^32 * 256 / fs)
 * and compute inc = (freq_q16 * K) >> 24, in 64-bit MULTIPLICATION only.
 *
 * K fits in 32 bits for every audio rate (44100 -> 24932236), and the worst
 * case product (20 kHz in Q16 times K) is about 3.3e16, far inside uint64.
 * The residual tuning error is under 0.0001 cent, i.e. inaudible.
 */
#define FM1_K_44100  24932236u
#define FM1_K_48000  22906492u

static inline uint32_t fm1_phase_inc(q16_t freq_q16, uint32_t k_samplerate)
{
    return (uint32_t)(((uint64_t)freq_q16 * (uint64_t)k_samplerate) >> 24);
}

/* Hz as an integer -> Q16, for callers that do not need fractional pitch. */
static inline q16_t fm1_hz(uint32_t hz) { return hz << 16; }

/* --- drive ----------------------------------------------------------------
 *
 * A cubic soft saturator, normalised so full scale in is full scale out:
 *
 *     y = 1.5u - 0.5u^3        for |u| <= 1
 *     y = sign(u)              beyond
 *
 * Chosen over the quadratic knee the master limiter uses, because the two
 * jobs are opposite. A limiter must be transparent until it is needed, so it
 * is flat below 0.5 and only bends near the rail. A drive stage should be
 * doing something at every level, and this curve is gently nonlinear from
 * zero -- which is what makes it read as gain rather than as a gate opening
 * onto distortion.
 *
 * Odd symmetric, so it generates odd harmonics only: third, fifth, no second.
 * That is the warm direction rather than the fuzzy one, and it keeps the
 * signal DC-free, which matters going straight into a DAW.
 *
 * Two multiplies and a shift.
 */
static inline q15_t fm1_saturate(int32_t x)
{
    /* Computed on the magnitude, with the sign reapplied at the end.
     *
     * Doing it directly on a signed value breaks the odd symmetry this curve
     * exists to have: an arithmetic right shift rounds toward negative
     * infinity, so the negative half comes out one LSB different from the
     * positive one. That is a DC offset and a second harmonic -- precisely
     * the two things an odd-symmetric saturator is chosen to avoid. */
    int32_t a = x < 0 ? -x : x;
    if (a >= Q15_ONE) return (q15_t)(x < 0 ? -Q15_ONE : Q15_ONE);

    int32_t a2 = (a * a) >> Q15_BITS;
    int32_t a3 = (a2 * a) >> Q15_BITS;
    int32_t y  = (3 * a - a3) >> 1;
    if (y > Q15_ONE) y = Q15_ONE;

    return (q15_t)(x < 0 ? -y : y);
}

/* The SUM page's rolling trace: one peak per slice of frames. 120 points
 * across a 240 px screen is two pixels each, and 32 frames a point fills
 * the window in about 93 ms. */
#define FM1_SCOPE_POINTS 120
#define FM1_SCOPE_SLICE   32

#endif /* FM1_FIXED_H */
