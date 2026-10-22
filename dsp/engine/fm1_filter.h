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
#include "../shared/fm1_svf_table.h"

/* The mode list and its names. Deliberately at global scope, not inside
 * namespace fm1: these are parameter VALUES, and they belong with the P_*
 * parameter indices in fm1_params.h, which are also global. Presets, tests
 * and probes name both in the same breath.
 *
 * They live in their own header because this one embeds braids::Svf and is
 * therefore C++ only, while the firmware's UI is C and has to put a mode name
 * on the screen. */
#include "fm1_fmode.h"

/* Slope: FP_6 / FP_12 / FP_24. Its own header for the same reason, and
 * included HERE rather than lower down -- these are parameter values at
 * global scope, and inside `namespace fm1` they would become fm1::FP_24 and
 * stop matching the C mirror. */
#include "fm1_fpole.h"

namespace fm1 {

struct fm1_filter_t {
    uint8_t     mode;
    uint8_t     cut, res, mix;      /* raw 0..127, as the knobs read */
    /* The cutoff again, in 1/128 semitone -- what the filter is actually
     * tuned to. `cut` is this rounded down, kept for the screen, the patch
     * and the CC map. Two names for one value because they have different
     * audiences: the player reads 128 steps, the filter hears 16384. */
    int32_t     cut_p;
    uint8_t     poles;              /* FP_* -- 6, 12 or 24 dB per octave */
    int16_t     key;                /* the voice's pitch, 1/128 semitone */

    braids::Svf svf;                /* a couple of dozen bytes */
    braids::Svf svf2;               /* the second pair, for 24 dB */
    int32_t     op;                 /* the one-pole's state, for 6 dB */
    /* The one-pole's coefficient, derived with the two SVF frequencies in
     * fm1_filter_apply_cut(). It belongs beside them: all three come from
     * cut_p and all three change only when cut_p does. */
    int32_t     fc;
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
    int32_t hz = f->cut_p;

    if (hz < 0)         hz = 0;
    if (hz > 127 * 128) hz = 127 * 128;
    f->svf.set_frequency((int16_t)hz);
    f->svf2.set_frequency((int16_t)hz);
    /* Derived HERE, not in the sample loop.
     *
     * The 6 dB branch used to read it per sample -- originally one indexed
     * flash load, and then, once the interpolation went in, two loads plus
     * two clamps and a lerp. Flash stalls about 3x on this part, so the
     * coefficient cost roughly four times the one-pole it feeds, on a branch
     * whose own comment calls it the cheapest block in the voice.
     *
     * It depends only on cut_p, and every write to cut_p comes through this
     * function -- init, reset, set_cut and set_cut_p all end here. */
    f->fc = fm1_svf_cut_q15(hz);
}

static inline void fm1_filter_set_res_raw(fm1_filter_t *f, uint8_t res)
{
    /* 0..127 -> 0..32766 for the SVF, which reads it through lut_svf_damp, so
     * the curve is already shaped and a linear map here is correct. */
    f->svf.set_resonance((int16_t)((int32_t)res * 258));
    /* The second pair runs at LOWER resonance than the first. Two cascaded
     * filters each at full Q is not a steeper filter, it is an oscillator:
     * the peaks multiply and the pair screams well before the knob reaches
     * the top. Two thirds keeps 24 dB usable across the whole range. */
    f->svf2.set_resonance((int16_t)(((int32_t)res * 258 * 2) / 3));
}

static inline void fm1_filter_init(fm1_filter_t *f)
{
    f->mode = F_OFF;
    f->cut  = 64;
    f->cut_p = 64 << 7;
    f->res  = 0;
    f->mix  = 127;          /* fully wet; OFF is how you bypass */
    f->poles = FP_12;       /* what it was before the knob existed */
    f->op   = 0;
    f->key  = 60 * 128;
    f->svf.Init();
    f->svf2.Init();
    fm1_filter_apply_cut(f);
    fm1_filter_set_res_raw(f, f->res);
}

/* Reset the audio state without touching the settings. Called on a mode
 * change: the integrators hold a value that means nothing in the new mode,
 * and letting it out is a click. */
static inline void fm1_filter_reset(fm1_filter_t *f)
{
    f->svf.Init();
    f->svf2.Init();
    f->op = 0;
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
    braids::SvfMode m = mode == F_HP ? braids::SVF_MODE_HP
                      : mode == F_LP ? braids::SVF_MODE_LP
                                     : braids::SVF_MODE_BP;   /* BP, NOTCH */
    f->svf.set_mode(m);
    f->svf2.set_mode(m);
}

static inline void fm1_filter_set_poles(fm1_filter_t *f, uint8_t poles)
{
    if (poles >= FP_NPOLES || poles == f->poles) return;
    f->poles = poles;
    f->op = 0;              /* the one-pole's state means nothing at 12 or 24 */
    f->svf2.Init();
}

/* Cutoff in 1/128 SEMITONE -- the unit the pitch, the comb and the key all
 * speak. The coarse byte is kept beside it for the screen and the patch.
 *
 * This is what makes a MODULATED sweep smooth. braids::Svf has always
 * interpolated its frequency (Interpolate824 over lut_svf_cutoff), but it
 * only ever received `cut << 7` with cut an integer, so an LFO on the cutoff
 * moved in 128 semitone jumps however finely the filter could have been
 * tuned. The resolution was there the whole way down and the parameter
 * threw it away before it arrived. */
static inline void fm1_filter_set_cut_p(fm1_filter_t *f, int32_t pitch)
{
    if (pitch < 0)         pitch = 0;
    if (pitch > 127 * 128) pitch = 127 * 128;
    f->cut_p = pitch;
    f->cut   = (uint8_t)(pitch >> 7);
    fm1_filter_apply_cut(f);
}

static inline void fm1_filter_set_cut(fm1_filter_t *f, uint8_t cut)
{
    f->cut = cut;
    f->cut_p = (int32_t)cut << 7;
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

/* The voice's pitch. The filter does not track it -- the cutoff is absolute
 * -- but the slots do, so this is kept for symmetry and for anything later
 * that wants it. */
static inline void fm1_filter_set_key(fm1_filter_t *f, int16_t pitch)
{
    f->key = pitch;
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
         * difference is the notch, and it costs one subtract. Always 12 dB:
         * cascading a notch deepens it rather than steepening it. */
        w = fm1_q31_to_q15_sat((int32_t)x - f->svf.Process(x));
        break;
    case F_LP: case F_HP:
        if (f->poles == FP_6) {
            /* One pole, from the same coefficient table the SVF reads, and
             * once again the cheapest block in the voice: one multiply, one
             * add, with the coefficient prepared at knob rate.
             *
             * Interpolated, because the 12 and 24 dB paths are: braids::Svf
             * runs its frequency through Interpolate824, so only this branch
             * landed on whole semitones. Switching SLOPE from 12 to 6 dB used
             * to move the corner by up to half a semitone as a side effect,
             * which is not what that knob is for. */
            /* ROUNDED, not floored. An arithmetic shift biases every step
             * by half an LSB, and a one-pole INTEGRATES that: the state
             * settles at -0.5 * 32768 / fc and stays there. Measured at CUT 0
             * (fc = 38) it is -431 LSB, -37.6 dBFS of DC on every voice --
             * and the sign inverts in HIGHPASS, where the output is x - op.
             * Four voices of it is -25.6 dBFS going straight into the DAW,
             * plus a thump whenever a gate opens. */
            f->op += (f->fc * ((int32_t)x - f->op) + (1 << (Q15_BITS - 1)))
                     >> Q15_BITS;
            w = (f->mode == F_LP) ? f->op : ((int32_t)x - f->op);
            w = fm1_q31_to_q15_sat(w);
            break;
        }
        if (f->poles == FP_24) {
            w = fm1_q31_to_q15_sat(f->svf2.Process(
                    fm1_q31_to_q15_sat(f->svf.Process(x))));
            break;
        }
        /* fall through: 12 dB is the plain SVF */
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
