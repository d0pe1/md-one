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

/* What CUT means depends on the mode, and the screen must say so -- a knob
 * called CUTOFF that sets a delay ratio is a knob nobody trusts. */
static const char *const FM1_FMODE_CUT_LABEL[F_NMODES] = {
    "CUTOFF", "CUTOFF", "CUTOFF", "CUTOFF", "TUNE", "OFFSET"
};

/* Likewise RES: feedback in a comb is not resonance in a filter, even though
 * both sharpen the peak. */
static const char *const FM1_FMODE_RES_LABEL[F_NMODES] = {
    "RES", "RES", "RES", "RES", "FEEDBK", "RES"
};

#endif /* FM1_FMODE_H */
