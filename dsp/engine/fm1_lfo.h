/* SPDX-License-Identifier: MIT
 *
 * The LFOs: four shaped, rate-controlled modulators, shared by every voice.
 *
 * Shared rather than per-voice, and that is the whole point of them. Per-voice
 * independence already exists -- fm1_drift.h wanders freely inside each voice,
 * at its own rate, from its own seed. Four global LFOs give the opposite and
 * more useful thing: COHERENT movement, all four voices breathing together,
 * which is a sound the drift cannot make however it is set.
 *
 * They run at BLOCK rate, 172.27 Hz. Everything they modulate is decided once
 * a block anyway (docs/SIGNAL_PATH.md section 8), so a per-sample LFO would
 * cost 256 times as much to produce a value nothing reads. That caps them
 * around 86 Hz, and the table stops well below it at 20 Hz -- audio-rate
 * modulation is what the FM operators are for.
 */
#ifndef FM1_LFO_H
#define FM1_LFO_H

#include "../shared/fm1_fixed.h"
#include "../shared/fm1_lfo_table.h"
#include "fm1_params.h"   /* LFP_* */
/* The oscillator's table, read at a 64th of its resolution -- an LFO
 * moves a parameter, not a speaker, and 1024 points across a cycle is
 * already finer than anything downstream can use. Exact beats a
 * parabola here for the cost of a table we already generate. */
#include "../shared/fm1_sine_table.h"

/* Order is the order the knob steps through: the four periodic shapes first,
 * then the two random ones. S&H is a MODE rather than a separate module --
 * same cost, one less concept, and it means you can have four of them. */
enum {
    /* SINE first, and the default. It is the shape an LFO is assumed to be
     * until told otherwise, and it was missing entirely -- the list went
     * straight to the triangle, which is the cheap approximation of it. */
    LFO_SINE = 0,
    LFO_TRI,
    LFO_SAW,            /* falling */
    LFO_RAMP,           /* rising */
    LFO_SQR,
    LFO_SH,             /* sample and hold: a new level each cycle */
    LFO_SMOOTH,         /* the same levels, interpolated: random but gradual */
    LFO_NMODES
};

/* Six characters at most, to match the knob column (fm1_pages.h). */
static const char *const FM1_LFO_MODE_NAME[LFO_NMODES] = {
    "SINE", "TRI", "SAW", "RAMP", "SQR", "S&H", "SMOOTH"
};

#define FM1_LFOS 4

typedef struct {
    uint32_t phase;         /* full 32 bits span one cycle; wraps for free */
    uint32_t inc;           /* added per BLOCK, from fm1_lfo_inc[] */
    uint32_t offset;        /* PHASE knob, as a fraction of a cycle */
    uint32_t rng;           /* per-LFO, or four S&H modes would agree */
    int16_t  cur, nxt;      /* the random modes' endpoints */
    uint8_t  mode;          /* the WAVE */
    uint8_t  amount;        /* 0..127 */
    uint32_t base_inc;      /* the rate asked for, before JITTER bends it */
    uint8_t  jitter;        /* how far the period wanders, cycle to cycle */
    uint8_t  energy;        /* how far the random modes jump */
    uint8_t  pol;           /* LFP_* */
} fm1_lfo_t;

/* xorshift32. Cheap, and its only job is to sound unrelated to itself. */
static inline uint32_t fm1_lfo_rand(fm1_lfo_t *l)
{
    uint32_t x = l->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    l->rng = x ? x : 0x1234567u;    /* zero is a fixed point: never let it in */
    return l->rng;
}

static inline void fm1_lfo_init(fm1_lfo_t *l, uint32_t seed)
{
    l->phase = 0;
    l->inc = fm1_lfo_inc[0];
    l->offset = 0;
    l->rng = seed ? seed : 0x9E3779B9u;
    l->mode = LFO_SINE;
    l->base_inc = l->inc;
    l->jitter = 0;
    l->energy = 127;
    l->pol = LFP_BI;
    l->amount = 0;
    l->cur = 0;
    l->nxt = (int16_t)((int32_t)(fm1_lfo_rand(l) >> 16) - 32768);
}

/* The phase increment for a Q8 knob position -- the byte plus its fraction.
 *
 * Shared with the drift, which runs on the same table so the two speak the
 * same hertz. Linear between adjacent entries: the table is exponential, but
 * one step is a ratio of 1.062, and interpolating a 6% span linearly is
 * within 0.1% of the geometric answer. Nobody hears 0.1% of an LFO rate. */
static inline uint32_t fm1_lfo_inc_q8(int32_t q8)
{
    int32_t i, f;
    uint32_t a, b;

    if (q8 < 0) q8 = 0;
    if (q8 > (FM1_LFO_RATES - 1) << 8) q8 = (FM1_LFO_RATES - 1) << 8;
    i = q8 >> 8;
    f = q8 & 255;
    if (i >= FM1_LFO_RATES - 1 || !f) return fm1_lfo_inc[i];
    a = fm1_lfo_inc[i];
    b = fm1_lfo_inc[i + 1];
    return a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8);
}

static inline void fm1_lfo_set_rate_q8(fm1_lfo_t *l, int32_t q8)
{
    l->base_inc = fm1_lfo_inc_q8(q8);
    if (!l->jitter) l->inc = l->base_inc;
}
static inline void fm1_lfo_set_jitter(fm1_lfo_t *l, uint8_t v)
{
    l->jitter = v;
    if (!v) l->inc = l->base_inc;       /* straight back to the asked rate */
}
static inline void fm1_lfo_set_energy(fm1_lfo_t *l, uint8_t v) { l->energy = v; }
static inline void fm1_lfo_set_pol(fm1_lfo_t *l, uint8_t v)
{ l->pol = v < LFP_NPOL ? v : LFP_BI; }

/* Restart the cycle. TRIG bound to a voice calls this when that voice
 * opens, which is the difference between an LFO that happens to be
 * somewhere and one that starts WITH the note. */
static inline void fm1_lfo_retrig(fm1_lfo_t *l)
{
    l->phase = 0;
    l->cur = l->nxt;
}
static inline void fm1_lfo_set_rate(fm1_lfo_t *l, uint8_t rate)
{
    fm1_lfo_set_rate_q8(l, (int32_t)rate << 8);
}

static inline void fm1_lfo_set_mode(fm1_lfo_t *l, uint8_t mode)
{
    if (mode < LFO_NMODES) l->mode = mode;
}

static inline void fm1_lfo_set_amount(fm1_lfo_t *l, uint8_t amount)
{
    l->amount = amount;
}

/* PHASE shifts where in the cycle the output is read from rather than
 * restarting the LFO. Turning it on a running modulator should slide the
 * waveform under you, not retrigger it: two LFOs set a quarter apart is a
 * thing people want, and it must not depend on when each was last touched. */
static inline void fm1_lfo_set_phase(fm1_lfo_t *l, uint8_t phase)
{
    l->offset = (uint32_t)phase << 25;      /* 128 steps spans a full cycle */
}

/* One block. Returns the modulation, Q15 bipolar, already scaled by AMOUNT.
 *
 * AMOUNT scales by >> 7 rather than / 127, so full scale is 127/128 of the
 * range -- 0.07 dB short, and worth it to keep a divide out of something that
 * runs four times a block forever.
 */
static inline q15_t fm1_lfo_block(fm1_lfo_t *l)
{
    uint32_t before = l->phase;
    uint32_t p;
    int32_t v;

    l->phase += l->inc;
    /* Unsigned wrap IS the cycle boundary: no counter, no compare against a
     * period, and it stays exact however long the LFO runs. */
    if (l->phase < before) {
        int32_t nx;
        l->cur = l->nxt;
        nx = (int32_t)(fm1_lfo_rand(l) >> 16) - 32768;
        /* ENERGY scales how far a random mode is willing to jump. At zero
         * the levels collapse toward the middle and S&H becomes a slow
         * shimmer rather than a staircase; at full it uses the whole
         * range, which is what it always did. */
        if (l->energy < 127) nx = (nx * (int32_t)l->energy) / 127;
        l->nxt = (int16_t)nx;

        /* JITTER bends the NEXT period, not this one -- a rate that changed
         * mid-cycle would be a glitch rather than a wander. Chosen once per
         * cycle, so a jittered LFO is still periodic, just not regular. */
        if (l->jitter) {
            uint32_t r = fm1_lfo_rand(l) >> 17;         /* 0..32767 */
            int32_t  d = ((int32_t)r - 16384) * (int32_t)l->jitter / 127;
            /* +-50% of the period at full jitter. Clamped above zero: an
             * increment of 0 is an LFO that stops. */
            int32_t  inc = (int32_t)(l->base_inc
                         + (int32_t)(((int64_t)l->base_inc * d) >> 15));
            l->inc = (uint32_t)(inc < 1 ? 1 : inc);
        }
    }

    p = (l->phase + l->offset) >> 16;       /* 0..65535 within the cycle */

    switch (l->mode) {
    case LFO_SINE: {
        /* p is 0..65535 across the cycle and the table is 1024 points with
         * a guard entry, so this is an index and a sixth of interpolation.
         * The guard is what lets the last step reach for t[1024] without a
         * wrap test. */
        uint32_t ti = p >> 6, tf = p & 63u;
        int32_t a = fm1_sine_table[ti], b = fm1_sine_table[ti + 1];
        v = a + (((b - a) * (int32_t)tf) >> 6);
        break;
    }
    case LFO_TRI:
        v = (p < 32768u) ? (int32_t)p * 2 - 32768
                         : 98304 - (int32_t)p * 2;
        break;
    case LFO_SAW:
        v = 32767 - (int32_t)p;
        break;
    case LFO_RAMP:
        v = (int32_t)p - 32768;
        break;
    case LFO_SQR:
        v = (p < 32768u) ? 32767 : -32768;
        break;
    case LFO_SH:
        v = l->cur;
        break;
    case LFO_SMOOTH:
    default: {
        /* Linear between this cycle's level and the next. Not a curve: the
         * corner at each endpoint is inaudible on something whose whole job
         * is to move slowly, and a cubic here would cost more than the rest
         * of the LFO put together.
         *
         * Both halves are shifted down one bit BEFORE the multiply, and that
         * is not fussiness. The endpoints each span the full signed range, so
         * their difference reaches +-65535, and frac reaches 65535:
         *
         *     65535 * 65535 = 4,294,836,225   > INT32_MAX
         *
         * which wraps negative and snaps the output to the opposite rail. It
         * only fires when two consecutive random endpoints land far apart, so
         * it shows up as an occasional click in whatever the LFO reaches --
         * roughly one cycle in two, found by a test rather than by ear.
         *
         * (delta/2) * (frac/2) >> 14 is the same value with two bits of
         * precision traded for a factor of four of headroom. */
        int32_t frac  = (int32_t)(l->phase >> 16);          /* 0..65535 */
        int32_t delta = (int32_t)l->nxt - (int32_t)l->cur;
        v = (int32_t)l->cur + (((delta >> 1) * (frac >> 1)) >> 14);
        break;
    }
    }

    if (v > Q15_ONE) v = Q15_ONE;
    if (v < Q15_MIN) v = Q15_MIN;

    /* POLARITY: which half of the swing comes out.
     *
     * Folded rather than clipped -- POS maps the whole cycle into 0..+1
     * instead of flattening its lower half, so the SHAPE survives and only
     * the range changes. A clipped triangle would be a triangle for half a
     * cycle and a flat line for the other, which is a different wave, not a
     * different polarity. */
    if (l->pol == LFP_POS)      v = (v + 32768) >> 1;
    else if (l->pol == LFP_NEG) v = (v - 32768) >> 1;

    return (q15_t)((v * (int32_t)l->amount) >> 7);
}

#endif /* FM1_LFO_H */
