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

#endif /* FM1_FIXED_H */
