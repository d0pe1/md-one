/* SPDX-License-Identifier: MIT
 *
 * Two insert slots per voice, between the filters.
 *
 *     OSC -> F1 -> [slot 1] -> [slot 2] -> F2 -> VCA -> EQ -> pan -> out
 *
 * These exist because COMB and RESO used to be MODES of the filter. That was
 * a cost argument and a correct one -- the comb measured cheaper than the SVF
 * (8,468 against 10,004), so folding it in was free -- but it was the wrong
 * structure. A comb is not a filter type, it is a resonator, and putting it
 * in the mode list meant you could have a lowpass OR a comb and never a
 * lowpass THROUGH a comb. Which is the interesting one.
 *
 * So the filter goes back to being a filter and the resonators move here,
 * where they can be stacked, reordered by which slot they sit in, and driven
 * into each other.
 *
 * MONO, deliberately. A voice is one signal until the pan stage, so a stereo
 * effect in here has nothing to work with: HAAS would have no second channel
 * to delay against and CHORUS no width to produce. Both are stereo by
 * construction and both already exist after the pan -- HAAS globally, CHORUS
 * in the patchbay. Offering them here would be offering a control that does
 * nothing, which this project has already shipped four of.
 */
#ifndef FM1_SLOT_H
#define FM1_SLOT_H

#include "../shared/fm1_fixed.h"
#include "fm1_comb.h"
#include "fm1_svf.h"

enum {
    SL_OFF = 0,     /* true bypass: not a multiply, not a branch in the loop */
    SL_DRIVE,       /* cubic saturator; P1 drive, P2 bias */
    SL_RESO,        /* keytracked resonant peak; P1 offset, P2 resonance */
    SL_COMB,        /* tuned feedback comb; P1 tune, P2 feedback */
    SL_FOLD,        /* wavefolder; P1 drive, P2 bias */
    SL_NTYPES
};

/* Six characters at most, to match the knob column (fm1_pages.h). */
static const char *const FM1_SLOT_NAME[SL_NTYPES] = {
    "OFF", "DRIVE", "RESO", "COMB", "FOLD"
};

/* What P1 and P2 mean here, because "P1" on a screen is a knob nobody
 * trusts -- the same reason the filter relabels CUT per mode. */
static const char *const FM1_SLOT_P1[SL_NTYPES] = {
    "----", "DRIVE", "OFFSET", "TUNE", "DRIVE"
};
static const char *const FM1_SLOT_P2[SL_NTYPES] = {
    "----", "BIAS", "RES", "FEEDBK", "BIAS"
};

typedef struct {
    uint8_t     type;
    uint8_t     p1, p2, mix;    /* raw 0..127, as the knobs read */
    int16_t     key;            /* the voice's pitch, 1/128 semitone */

    fm1_comb_t  comb;           /* SL_COMB */
    fm1_svf_t   svf;            /* SL_RESO */
} fm1_slot_t;

static inline void fm1_slot_init(fm1_slot_t *s)
{
    s->type = SL_OFF;
    s->p1 = 64;
    s->p2 = 0;
    s->mix = 127;
    s->key = 0;
    fm1_comb_init(&s->comb);
    fm1_svf_init(&s->svf);
}

/* The wavefolder, O(1): a triangle of period 2^17 over the input, so a
 * hard drive reflects as many times as it needs to without a loop that can
 * run away. Copied in spirit from the patchbay's fx_fold1 and NOT shared
 * with it -- that one lives in fm1_rack.h, which embeds braids::Svf and is
 * C++ only, and this header has to stay reachable from the firmware's C. */
static inline int32_t fm1_slot_fold(int32_t x, int32_t drive_q12, int32_t bias)
{
    uint32_t u;
    int32_t  y;

    x = (x * drive_q12) >> Q12_BITS;
    x += bias;

    u = (uint32_t)x & 0x1FFFFu;
    if (u < 32768u)      y = (int32_t)u;
    else if (u < 98304u) y = 65536 - (int32_t)u;
    else                 y = (int32_t)u - 131072;

    return y > Q15_ONE ? Q15_ONE : y;
}

/* --- block-rate setup ----------------------------------------------------
 *
 * Everything that only changes when a knob moves is decided here, once per
 * block, never per sample. That rule has been relearned three times in this
 * project at a cost of several percent of the audio budget each time
 * (docs/SIGNAL_PATH.md section 8), so the per-sample function below does
 * nothing but arithmetic on values this one prepared.
 */
static inline void fm1_slot_set_key(fm1_slot_t *s, int16_t key, uint32_t inc)
{
    s->key = key;
    if (s->type == SL_COMB) {
        /* ratio from P1: 64 is unison. The comb derives its delay from the
         * voice's phase increment, which is how it tunes without a 64-bit
         * divide anywhere in the control path. */
        int32_t ratio_q12 = 2048 + ((int32_t)s->p1 * 2048) / 127;
        fm1_comb_set_from_inc(&s->comb, inc, ratio_q12);
    } else if (s->type == SL_RESO) {
        /* P1 is an offset in SEMITONES from the note, so the peak tracks the
         * voice instead of sitting at a fixed frequency -- on a drone that is
         * the difference between a struck body and a whistle. key is in
         * 1/128 semitone, and the SVF wants a note. */
        /* Tuned from the FULL pitch, not from `key >> 7`. That shift floored
         * to a whole semitone, which gave the resonator the comb's bug
         * exactly: a note on the grid is N*128, one cent flat is N*128 - 1,
         * and that floors to N - 1 -- so it dropped a semitone on the first
         * click below centre and did not move at all for fifty cents sharp. */
        int32_t semis = ((int32_t)s->p1 - 64) * 24 / 64;
        fm1_svf_set_pitch(&s->svf, (int32_t)key + semis * 128);
    }
}

static inline void fm1_slot_set_type(fm1_slot_t *s, uint8_t t)
{
    if (t >= SL_NTYPES || t == s->type) return;
    s->type = t;
    /* A slot switching type must not play out what the previous one left in
     * its delay line -- that is a click and a fragment of the wrong sound. */
    fm1_comb_init(&s->comb);
    fm1_svf_init(&s->svf);
}

static inline void fm1_slot_set_p1(fm1_slot_t *s, uint8_t v) { s->p1 = v; }
static inline void fm1_slot_set_p2(fm1_slot_t *s, uint8_t v)
{
    s->p2 = v;
    /* Hard-limited below unity by fm1_comb_set_feedback, or the comb never
     * decays; the same scaling the filter's comb mode uses. */
    if (s->type == SL_COMB) fm1_comb_set_feedback(&s->comb, ((int32_t)v * 31000) / 127);
    else if (s->type == SL_RESO) fm1_svf_set_res(&s->svf, v);
}
static inline void fm1_slot_set_mix(fm1_slot_t *s, uint8_t v) { s->mix = v; }

/* Is this slot doing anything? Asked once per block so an OFF slot costs a
 * compare rather than a branch inside the sample loop. */
static inline int fm1_slot_idle(const fm1_slot_t *s)
{
    return s->type == SL_OFF || s->mix == 0;
}

/* --- per sample ----------------------------------------------------------
 *
 * Dry/wet inside the slot rather than outside it, so MIX works the same way
 * on every type and a slot at mix 0 is bit-exactly bypassed.
 */
static inline q15_t fm1_slot_tick(fm1_slot_t *s, q15_t in)
{
    int32_t wet;

    switch (s->type) {
    case SL_DRIVE: {
        /* The same cubic the mixer's DRIVE uses: odd symmetric, so third
         * and fifth but no second harmonic and no DC. P2 biases the input,
         * which breaks that symmetry on purpose when you want it to. */
        int32_t x = (int32_t)in + (((int32_t)s->p2 - 64) << 6);
        int32_t g = Q12_ONE + ((int32_t)s->p1 * Q12_ONE) / 32;
        wet = fm1_saturate((x * g) >> 12);
        break;
    }
    case SL_RESO: {
        /* The band pass IS the resonant peak: everything away from the
         * centre falls off, which is what makes it read as a struck body
         * rather than as a filter sweep. */
        fm1_svf_out_t o = fm1_svf_tick(&s->svf, in);
        wet = o.bp;
        break;
    }
    case SL_COMB:
        wet = fm1_comb_tick(&s->comb, in);
        break;
    case SL_FOLD: {
        int32_t x = (int32_t)in + (((int32_t)s->p2 - 64) << 6);
        int32_t g = Q12_ONE + ((int32_t)s->p1 * 3 * Q12_ONE) / 127;
        wet = fm1_slot_fold(x, g, 0);
        (void)0;
        break;
    }
    default:
        return in;
    }

    if (s->mix >= 127) return (q15_t)wet;
    return (q15_t)(((int32_t)in * (127 - s->mix) + wet * s->mix) / 127);
}

#endif /* FM1_SLOT_H */
