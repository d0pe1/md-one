/* SPDX-License-Identifier: MIT
 *
 * The per-voice multimode filter.
 *
 * Two of these sit in every voice:
 *
 *     OSC -> F1 -> VCA -> [slot 1] -> [slot 2] -> F2 -> EQ -> pan
 *
 * A plain state-variable filter and nothing else. The comb and the resonator
 * used to be MODES here, on the measurement that the comb (8,468 cycles) is
 * cheaper than the SVF (10,004) so folding it in was free. The arithmetic
 * held; the structure did not. A resonator is not a filter type, and having
 * it in the mode list meant a voice could have a lowpass OR a comb and never
 * a lowpass THROUGH a comb. They live in the insert slots now (fm1_slot.h).
 *
 * The delay line went with them, so this struct lost 4 KiB -- and the slots
 * gained it back, two per voice instead of two filters per voice. Net zero,
 * and now the RAM is where the resonators actually are.
 *
 * NOTE: this header is C++ only, because braids::Svf is a class.
 */
#ifndef FM1_FILTER_H
#define FM1_FILTER_H

#include "../mi/braids/svf.h"
#include "../shared/fm1_fixed.h"

/* The mode list and its names. Deliberately at global scope, not inside
 * namespace fm1: these are parameter VALUES, and they belong with the P_*
 * parameter indices in fm1_params.h, which are also global. Presets, tests
 * and probes name both in the same breath.
 *
 * They live in their own header because this one embeds braids::Svf and is
 * therefore C++ only, while the firmware's UI is C and has to put a mode name
 * on the screen. */
#include "fm1_fmode.h"

namespace fm1 {

struct fm1_filter_t {
    uint8_t     mode;
    uint8_t     cut, res, mix;      /* raw 0..127, as the knobs read */
    uint8_t     track;              /* KEYTRACK: 0 fixed, 127 follows the note */
    int16_t     key;                /* the voice's pitch, 1/128 semitone */

    braids::Svf svf;                /* a couple of dozen bytes */
};

static inline void fm1_filter_apply_cut(fm1_filter_t *f);

/* --- parameter mapping -----------------------------------------------------
 *
 * CUT is an absolute cutoff in every mode now that the resonators have moved
 * to the insert slots: 0..127 read as a MIDI note. The SVF's frequency unit
 * is semitone<<7, the same unit Braids uses for pitch, so this is a shift.
 *
 * KEYTRACK is the one thing that bends it. At 0 the cutoff is fixed, which is
 * what you want when the filter is shaping a formant; at 127 it follows the
 * note one-for-one, which is what you want when it is part of the voice. The
 * useful settings are usually between, and there was no way to ask for any of
 * them before -- the knob did not exist.
 */
static inline void fm1_filter_apply_cut(fm1_filter_t *f)
{
    int32_t hz = (int32_t)f->cut << 7;

    if (f->track) {
        /* The note, relative to middle C, scaled by the amount. Relative so
         * that KEYTRACK alone does not also transpose the cutoff -- turning
         * it up must not change the sound of the note you are already on. */
        int32_t rel = (int32_t)f->key - 60 * 128;
        hz += (rel * (int32_t)f->track) / 127;
    }
    if (hz < 0)         hz = 0;
    if (hz > 127 * 128) hz = 127 * 128;
    f->svf.set_frequency((int16_t)hz);
}

static inline void fm1_filter_set_res_raw(fm1_filter_t *f, uint8_t res)
{
    /* 0..127 -> 0..32766 for the SVF, which reads it through lut_svf_damp, so
     * the curve is already shaped and a linear map here is correct. */
    f->svf.set_resonance((int16_t)((int32_t)res * 258));
}

static inline void fm1_filter_init(fm1_filter_t *f)
{
    f->mode = F_OFF;
    f->cut  = 64;
    f->res  = 0;
    f->mix  = 127;          /* fully wet; OFF is how you bypass */
    f->track = 0;
    f->key  = 60 * 128;
    f->svf.Init();
    fm1_filter_apply_cut(f);
    fm1_filter_set_res_raw(f, f->res);
}

/* Reset the audio state without touching the settings. Called on a mode
 * change: the integrators hold a value that means nothing in the new mode,
 * and letting it out is a click. */
static inline void fm1_filter_reset(fm1_filter_t *f)
{
    f->svf.Init();
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
    /* NOTCH runs the SVF in BP and subtracts at the output, because
     * braids::Svf returns one response and has no notch of its own. */
    f->svf.set_mode(mode == F_HP ? braids::SVF_MODE_HP
                  : mode == F_LP ? braids::SVF_MODE_LP
                                 : braids::SVF_MODE_BP);   /* BP and NOTCH */
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

/* Called when the voice's pitch changes. Only matters while KEYTRACK is up:
 * at zero the cutoff does not follow the note and there is nothing to redo. */
static inline void fm1_filter_set_key(fm1_filter_t *f, int16_t pitch)
{
    f->key = pitch;
    if (f->track) fm1_filter_apply_cut(f);
}

static inline void fm1_filter_set_track(fm1_filter_t *f, uint8_t track)
{
    f->track = track;
    fm1_filter_apply_cut(f);
}

/* --- audio ----------------------------------------------------------------- */

static inline q15_t fm1_filter_tick(fm1_filter_t *f, q15_t x)
{
    int32_t w;

    switch (f->mode) {
    case F_OFF:
        return x;
    case F_NOTCH:
        /* in - bp. The band pass is everything the notch removes, so the
         * difference is the notch, and it costs one subtract. */
        w = fm1_q31_to_q15_sat((int32_t)x - f->svf.Process(x));
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
