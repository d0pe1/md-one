/* SPDX-License-Identifier: MIT
 *
 * The per-voice multimode filter.
 *
 * Two of these sit in every voice: OSC -> F1 -> VCA -> F2 -> pan. Both run the
 * same mode list, so "filter before or after the comb" is a per-patch choice
 * rather than an architecture decision (docs/SIGNAL_PATH.md section 5b).
 *
 * The comb is a MODE here, not a separate block. That is the Serum shape --
 * comb and flanger are filter types, because they are the pitch-tracking part
 * and the filter is the per-voice block -- and the measurements said it costs
 * nothing to do it that way:
 *
 *     SVF   10,004 cycles/voice/block
 *     comb   8,468 cycles/voice/block
 *
 * The block that tracks pitch is the CHEAPER one, so folding it in is not a
 * compromise made to save cycles.
 *
 * NOTE: this header is C++ only, because braids::Svf is a class. C code that
 * wants the comb alone includes fm1_comb.h directly, as dsp/tests/test_comb.c
 * does.
 */
#ifndef FM1_FILTER_H
#define FM1_FILTER_H

#include "../mi/braids/svf.h"
#include "../shared/fm1_fixed.h"
#include "fm1_comb.h"

/* Mode list. Order is the order the knob steps through, so the three cheap
 * state-variable modes come first and the two pitch-tracking ones last.
 *
 * Deliberately at global scope, not inside namespace fm1: these are parameter
 * VALUES, and they belong with the P_* parameter indices in fm1_params.h,
 * which are also global. Presets, tests and probes name both in the same
 * breath. */
enum {
    F_OFF = 0,      /* true bypass: costs nothing, not even a multiply */
    F_LP,           /* state-variable low pass */
    F_BP,           /* band pass */
    F_HP,           /* high pass */
    F_COMB,         /* tuned feedback comb; CUT is tune, RES is feedback */
    F_RESO,         /* keytracked resonant peak; CUT is an offset in semitones */
    F_NMODES
};

/* Eight characters at most, to match fm1_params.h's screen labels. */
static const char *const FM1_FMODE_NAME[F_NMODES] = {
    "OFF", "LOWPASS", "BANDPASS", "HIGHPASS", "COMB", "RESO"
};

namespace fm1 {

struct fm1_filter_t {
    uint8_t     mode;
    uint8_t     cut, res, mix;      /* raw 0..127, as the knobs read */
    int16_t     key;                /* the voice's pitch, 1/128 semitone */

    braids::Svf svf;                /* a couple of dozen bytes */
    fm1_comb_t  comb;               /* 4 KiB -- see the note on cost below */
};

/* On memory: every filter carries a comb delay line whether or not it is in
 * COMB mode, so two filters across four voices is ~33 KiB of pool even if all
 * eight are lowpass. That is deliberate. The pool has ~220 KiB free, there is
 * no allocator on this chip, and a mode switch that had to find memory could
 * fail inside the audio ISR. Spending idle RAM to make mode changes infallible
 * is the right trade here. */

static inline void fm1_filter_apply_cut(fm1_filter_t *f);

/* --- parameter mapping -----------------------------------------------------
 *
 * CUT means something different per mode, which is the point of a mode list:
 *
 *   LP/BP/HP   absolute cutoff, 0..127 read as a MIDI note. The SVF's own
 *              frequency unit is semitone<<7, the same unit Braids uses for
 *              pitch, so this is a direct shift.
 *   RESO       an OFFSET from the voice's pitch in semitones, centred at 64.
 *              The peak tracks the note, which is what makes it a resonator
 *              rather than a fixed formant.
 *   COMB       the delay ratio: 64 is unison, lower is up, higher is down.
 */
static inline void fm1_filter_apply_cut(fm1_filter_t *f)
{
    switch (f->mode) {
    case F_LP: case F_BP: case F_HP:
        f->svf.set_frequency((int16_t)((int32_t)f->cut << 7));
        break;
    case F_RESO: {
        int32_t hz = (int32_t)f->key + ((int32_t)f->cut - 64) * 128;
        if (hz < 0)          hz = 0;
        if (hz > 127 * 128)  hz = 127 * 128;
        f->svf.set_frequency((int16_t)hz);
        break;
    }
    default:
        break;      /* COMB is tuned from the phase increment, see set_key */
    }
}

static inline void fm1_filter_set_res_raw(fm1_filter_t *f, uint8_t res)
{
    /* 0..127 -> 0..32766 for the SVF, which reads it through lut_svf_damp, so
     * the curve is already shaped and a linear map here is correct. */
    f->svf.set_resonance((int16_t)((int32_t)res * 258));
    /* The same knob is the comb's feedback, hard-limited below unity by
     * fm1_comb_set_feedback or it never decays. */
    fm1_comb_set_feedback(&f->comb, ((int32_t)res * 31000) / 127);
}

static inline void fm1_filter_init(fm1_filter_t *f)
{
    f->mode = F_OFF;
    f->cut  = 64;
    f->res  = 0;
    f->mix  = 127;          /* fully wet; OFF is how you bypass */
    f->key  = 60 * 128;
    f->svf.Init();
    fm1_comb_init(&f->comb);
    fm1_filter_apply_cut(f);
    fm1_filter_set_res_raw(f, f->res);
}

/* Reset the audio state without touching the settings. Called on a mode change
 * so switching INTO comb does not replay whatever was left in the delay line
 * the last time it ran -- which is a burst of stale audio, possibly seconds
 * old. */
static inline void fm1_filter_reset(fm1_filter_t *f)
{
    f->svf.Init();
    fm1_comb_init(&f->comb);
    fm1_filter_apply_cut(f);
    fm1_filter_set_res_raw(f, f->res);
}

static inline void fm1_filter_set_mode(fm1_filter_t *f, uint8_t mode)
{
    if (mode >= F_NMODES || mode == f->mode) return;
    f->mode = mode;
    /* State is per-algorithm and meaningless across a switch; resetting also
     * avoids a click from whatever the integrators were holding. */
    fm1_filter_reset(f);
    f->svf.set_mode(mode == F_HP ? braids::SVF_MODE_HP
                  : mode == F_LP ? braids::SVF_MODE_LP
                                 : braids::SVF_MODE_BP);   /* BP and RESO */
}

static inline void fm1_filter_set_cut(fm1_filter_t *f, uint8_t cut)
{
    f->cut = cut;
    fm1_filter_apply_cut(f);
}

static inline void fm1_filter_set_res(fm1_filter_t *f, uint8_t res)
{
    f->res = res;
    fm1_filter_set_res_raw(f, res);
}

static inline void fm1_filter_set_mix(fm1_filter_t *f, uint8_t mix)
{
    f->mix = mix;
}

/* Called when the voice's pitch changes. `inc` is the oscillator's phase
 * increment, which is how the comb derives its delay without a 64-bit divide
 * (see fm1_comb.h). ratio_q12 scales that delay. */
static inline void fm1_filter_set_key(fm1_filter_t *f, int16_t pitch,
                                      uint32_t inc, int32_t ratio_q12)
{
    f->key = pitch;
    fm1_comb_set_from_inc(&f->comb, inc, ratio_q12);
    if (f->mode == F_RESO) fm1_filter_apply_cut(f);
}

/* --- audio ----------------------------------------------------------------- */

static inline q15_t fm1_filter_tick(fm1_filter_t *f, q15_t x)
{
    int32_t w;

    switch (f->mode) {
    case F_OFF:
        return x;
    case F_COMB:
        w = fm1_comb_tick(&f->comb, x);
        break;
    default:
        /* The SVF clips its own integrators to int16 internally, but HP is a
         * difference of two clipped terms and can land just outside. */
        w = fm1_q31_to_q15_sat(f->svf.Process(x));
        break;
    }

    if (f->mix >= 127) return (q15_t)w;
    return fm1_q31_to_q15_sat((int32_t)x + (((w - (int32_t)x) * f->mix) >> 7));
}

}  // namespace fm1

#endif /* FM1_FILTER_H */
