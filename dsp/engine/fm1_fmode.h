/* SPDX-License-Identifier: MIT
 *
 * The per-voice filter's mode list, split out so C can see it.
 *
 * These live apart from fm1_filter.h for one reason: that header embeds
 * braids::Svf and is therefore C++ only, while the firmware's UI is C and has
 * to put a mode NAME on the screen. The enum is a set of parameter VALUES, so
 * it belongs with the P_* indices in fm1_params.h -- which are also global,
 * and also plain C.
 *
 * fm1_filter.h includes this, so nothing that used the enum had to change.
 */
#ifndef FM1_FMODE_H
#define FM1_FMODE_H

/* Order is the order the knob steps through, so the three cheap
 * state-variable modes come first and the two pitch-tracking ones last. */
/* COMB and RESO are NOT here any more. They were modes of the filter because
 * the comb measured cheaper than the SVF (8,468 against 10,004), so folding
 * them in cost nothing -- a correct cost argument and the wrong structure. A
 * comb is a resonator, not a filter type, and having it in this list meant
 * you could have a lowpass OR a comb and never a lowpass THROUGH a comb.
 * They live in the voice's insert slots now (fm1_slot.h), where they can be
 * stacked and driven into each other.
 *
 * NOTCH takes the vacated space and is nearly free: the Chamberlin SVF
 * already computes it as (in - bp). */
enum {
    F_OFF = 0,      /* true bypass: costs nothing, not even a multiply */
    F_LP,           /* state-variable low pass */
    F_BP,           /* band pass */
    F_HP,           /* high pass */
    F_NOTCH,
    F_NMODES
};

/* Eight characters at most, to match fm1_params.h's screen labels. */
static const char *const FM1_FMODE_NAME[F_NMODES] = {
    "OFF", "LOWPASS", "BANDPASS", "HIGHPASS", "NOTCH"
};

/* CUT and RES mean the same thing in every mode now that the resonators have
 * moved out, so the per-mode relabelling the filter needed is gone with
 * them. The slots carry their own labels (FM1_SLOT_P1 / P2). */

#endif /* FM1_FMODE_H */
