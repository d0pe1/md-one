/* SPDX-License-Identifier: MIT */
#include "fm1_osc.h"
#include "fm1_sine_table.h"

/* Top FM1_SINE_BITS of the phase pick the entry; the next 16 bits are the
 * interpolation fraction. With a 1024-point table that leaves 6 bits of the
 * phase unused per sample, which is below the interpolation error anyway. */
#define IDX_SHIFT   (32 - FM1_SINE_BITS)
#define FRAC_SHIFT  (IDX_SHIFT - 16)

static inline q15_t lookup(uint32_t phase)
{
    uint32_t idx  = phase >> IDX_SHIFT;
    uint32_t frac = (phase >> FRAC_SHIFT) & 0xFFFFu;
    int32_t  a    = fm1_sine_table[idx];
    int32_t  b    = fm1_sine_table[idx + 1];       /* guard entry: no wrap */
    return (q15_t)(a + (((b - a) * (int32_t)frac) >> 16));
}

q15_t fm1_osc_sine(fm1_osc_t *o)
{
    q15_t s = lookup(o->phase);
    o->phase += o->inc;
    return s;
}

void fm1_osc_sine_block(fm1_osc_t *o, q15_t *out, uint32_t n)
{
    uint32_t phase = o->phase;      /* keep it in a register across the block */
    uint32_t inc   = o->inc;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = lookup(phase);
        phase += inc;
    }
    o->phase = phase;
}

q15_t fm1_osc_sine_pm(fm1_osc_t *o, q15_t mod)
{
    /* mod is Q15 of a whole cycle. Shifting left by 17 maps +-32767 onto
     * +-(2^32 / 2), i.e. +-half a cycle, and the unsigned add wraps correctly.
     * Cast through uint32_t so the shift of a negative value is well defined. */
    uint32_t off = (uint32_t)((int32_t)mod) << 17;
    q15_t    s   = lookup(o->phase + off);
    o->phase += o->inc;
    return s;
}
