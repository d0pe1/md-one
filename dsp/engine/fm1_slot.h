/* SPDX-License-Identifier: MIT
 *
 * Two insert slots per voice, between the filters.
 *
 *     OSC -> F1 -> VCA -> [slot 1] -> [slot 2] -> F2 -> EQ -> DRIVE -> pan
 *
 * (This line used to put the VCA after F2, which is not where it is. The VCA
 * sits BEFORE the slots -- fm1_drone.cc applies the envelope and LEVEL right
 * after F1 -- and that ordering is the whole reason a resonator keeps ringing
 * after a voice closes. fm1_filter.h had it right and this did not.)
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
#include "fm1_tpt.h"

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

/* Whether each is BIPOLAR, which the screen has to know or the control lies.
 *
 * A slot's P1 and P2 mean different things per type, and some of those
 * meanings are centred on 64 while others run from zero. TUNE is an octave
 * either way, OFFSET is +-24 semitones, BIAS pushes the waveshaper either
 * side of centre, and FEEDBACK is now signed. Shown as a plain 0..127 all of
 * them read as if 64 were "half", when for these it is "none" -- and a
 * player cannot find a centre detent that the display does not admit exists.
 *
 * 1 here means bipolar; the UI maps it to K_BIP. Kept as a flag rather than
 * the kind itself so this header stays free of fm1_params.h. */
static const uint8_t FM1_SLOT_P1_BIP[SL_NTYPES] = { 0, 0, 1, 1, 0 };
static const uint8_t FM1_SLOT_P2_BIP[SL_NTYPES] = { 0, 1, 0, 1, 1 };

typedef struct {
    uint8_t     type;
    uint8_t     p1, p2, mix;    /* raw 0..127, as the knobs read */
    /* P1 again, Q8, which is what the tuning is actually computed from.
     * `p1` is this rounded down and kept for the screen and the patch -- the
     * same two-names-one-value arrangement the filter cutoff uses, and for
     * the same reason: the player reads 128 steps, the resonator hears
     * 32768. */
    int32_t     p1_q8;
    int16_t     key;            /* the voice's pitch, 1/128 semitone */

    fm1_comb_t  comb;           /* SL_COMB */
    fm1_tpt_t   svf;            /* SL_RESO */
} fm1_slot_t;

static inline void fm1_slot_init(fm1_slot_t *s)
{
    s->type = SL_OFF;
    s->p1 = 64;
    s->p1_q8 = 64 << 8;
    /* 64, not 0. P2 is a BIAS on DRIVE and FOLD and a feedback sign on the
     * comb, all centred there -- a default of 0 meant a slot came up with
     * full negative bias the moment you chose one of those types. */
    s->p2 = 64;
    s->mix = 127;
    s->key = 0;
    fm1_comb_init(&s->comb);
    fm1_tpt_init(&s->svf);
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
        /* The comb derives its delay from the voice's phase increment,
         * which is how it tunes without a 64-bit divide in the control path.
         *
         * fm1_comb.h: 4096 IS the note, 8192 an octave down, 2048 an octave
         * up. This read `2048 + p1*2048/127`, spanning 2048..4096 -- that is
         * octave-up to unison. So the detent this comment used to call unison
         * was 3080, a fourth and a bit SHARP, and a comb tuned BELOW the note
         * was unreachable: half of what a resonator is for, missing, behind a
         * centre that was not centred.
         *
         * Centred on 4096 now, an octave either way. */
        /* From the Q8 position, so a fine click is a fraction of a step.
         * (64 << 8) is the centre and 2048/16384 is one eighth, so the span
         * is the same octave either way it always was -- only the grain
         * changes. Symmetric, because a shift floors and this is bipolar
         * about a centre detent. */
        int32_t d8 = s->p1_q8 - (64 << 8);
        int32_t ratio_q12 = 4096 + (d8 >= 0 ? (d8 + 4) >> 3 : -((-d8 + 4) >> 3));
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
        /* IN 1/128 SEMITONE, not in whole semitones.
         *
         * This was `semis = (p1 - 64) * 24 / 64` and then `semis * 128`, an
         * integer divide feeding a multiply -- so the offset snapped to whole
         * semitones and 128 knob positions produced 48 distinct tunings. A
         * resonator a semitone off its intended pitch is a different
         * instrument, which is precisely what the comb was found doing and
         * fixed for; the same fault was sitting one branch below.
         *
         * +-24 semitones over +-64 steps is 3/16 of a 1/128-semitone unit
         * per Q8 count, exactly. */
        int32_t d8 = s->p1_q8 - (64 << 8);
        int32_t off = d8 >= 0 ? (d8 * 3 + 8) >> 4 : -(((-d8) * 3 + 8) >> 4);
        fm1_tpt_set_pitch(&s->svf, (int32_t)key + off);
    }
}

/* Declared early: set_type has to reapply P2 after re-initialising the comb
 * and the SVF, and the definition sits below. */
static inline void fm1_slot_set_p2(fm1_slot_t *s, uint8_t v);

static inline void fm1_slot_set_type(fm1_slot_t *s, uint8_t t)
{
    if (t >= SL_NTYPES || t == s->type) return;
    s->type = t;
    /* A slot switching type must not play out what the previous one left in
     * its delay line -- that is a click and a fragment of the wrong sound. */
    fm1_comb_init(&s->comb);
    fm1_tpt_init(&s->svf);
    /* ...but those Inits also reset the FEEDBACK and the RESONANCE to their
     * defaults, and P2 was never reapplied -- so switching COMB -> DRIVE ->
     * COMB silently dropped the feedback to zero while the panel still read
     * 127. It got 12 dB LOUDER too, because the comb's input gain is scaled
     * against its feedback and jumped back to unity.
     *
     * Worse, the knob could not fix it: ApplyParam short-circuits on
     * `d.p[param] == value`, so re-sending the value already on screen did
     * nothing. The only recovery was to move the knob and move it back. */
    fm1_slot_set_p2(s, s->p2);
}

static inline void fm1_slot_set_p1_q8(fm1_slot_t *s, int32_t q8)
{
    if (q8 < 0) q8 = 0;
    if (q8 > 127 << 8) q8 = 127 << 8;
    s->p1_q8 = q8;
    s->p1 = (uint8_t)(q8 >> 8);
}
static inline void fm1_slot_set_p1(fm1_slot_t *s, uint8_t v)
{ fm1_slot_set_p1_q8(s, (int32_t)v << 8); }
static inline void fm1_slot_set_p2(fm1_slot_t *s, uint8_t v)
{
    s->p2 = v;
    /* Hard-limited below unity by fm1_comb_set_feedback, or the comb never
     * decays; the same scaling the filter's comb mode uses. */
    if (s->type == SL_COMB) {
        /* BIPOLAR, centred at 64. fm1_comb_set_feedback has always taken a
         * signed coefficient and clamped it at +-31000; the slot only ever
         * sent it the positive half, so half the comb was implemented and
         * unreachable.
         *
         * The sign is what sets WHICH harmonics peak. Positive feedback
         * resonates at every multiple of 1/delay. Negative inverts each
         * pass, so even multiples cancel and only the ODD ones peak -- the
         * spacing doubles and the tone goes hollow, which is the difference
         * between a plucked string and a stopped pipe. It is not a subtler
         * version of the same sound.
         *
         * It also lines COMB up with DRIVE and FOLD, whose P2 bias was
         * already bipolar about 64. */
        int32_t fb = ((int32_t)v - 64) * 31000 / 63;
        if (fb >  31000) fb =  31000;
        if (fb < -31000) fb = -31000;
        fm1_comb_set_feedback(&s->comb, fb);
    }
    else if (s->type == SL_RESO) fm1_tpt_set_res(&s->svf, v);
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
        fm1_tpt_out_t o = fm1_tpt_tick(&s->svf, in);
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
