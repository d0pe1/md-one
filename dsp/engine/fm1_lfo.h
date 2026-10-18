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

/* Order is the order the knob steps through: the four periodic shapes first,
 * then the two random ones. S&H is a MODE rather than a separate module --
 * same cost, one less concept, and it means you can have four of them. */
enum {
    LFO_TRI = 0,
    LFO_SAW,            /* falling */
    LFO_RAMP,           /* rising */
    LFO_SQR,
    LFO_SH,             /* sample and hold: a new level each cycle */
    LFO_SMOOTH,         /* the same levels, interpolated: random but gradual */
    LFO_NMODES
};

/* Six characters at most, to match the knob column (fm1_pages.h). */
static const char *const FM1_LFO_MODE_NAME[LFO_NMODES] = {
    "TRI", "SAW", "RAMP", "SQR", "S&H", "SMOOTH"
};

#define FM1_LFOS 4

typedef struct {
    uint32_t phase;         /* full 32 bits span one cycle; wraps for free */
    uint32_t inc;           /* added per BLOCK, from fm1_lfo_inc[] */
    uint32_t offset;        /* PHASE knob, as a fraction of a cycle */
    uint32_t rng;           /* per-LFO, or four S&H modes would agree */
    int16_t  cur, nxt;      /* the random modes' endpoints */
    uint8_t  mode;
    uint8_t  amount;        /* 0..127 */
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
    l->mode = LFO_TRI;
    l->amount = 0;
    l->cur = 0;
    l->nxt = (int16_t)((int32_t)(fm1_lfo_rand(l) >> 16) - 32768);
}

static inline void fm1_lfo_set_rate(fm1_lfo_t *l, uint8_t rate)
{
    l->inc = fm1_lfo_inc[rate < FM1_LFO_RATES ? rate : FM1_LFO_RATES - 1];
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
        l->cur = l->nxt;
        l->nxt = (int16_t)((int32_t)(fm1_lfo_rand(l) >> 16) - 32768);
    }

    p = (l->phase + l->offset) >> 16;       /* 0..65535 within the cycle */

    switch (l->mode) {
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
    return (q15_t)((v * (int32_t)l->amount) >> 7);
}

#endif /* FM1_LFO_H */
