/* SPDX-License-Identifier: MIT
 *
 * Per-voice channel EQ: three multimode bands.
 *
 *     OSC -> F1 -> VCA -> F2 -> [ EQ ] -> DRIVE -> pan -> route
 *
 * Each band is a filter with a mode, and each mode is built from the taps of
 * one state-variable filter (fm1_svf.h), so a band costs the same whatever it
 * is set to:
 *
 *     LOW    SHELF  in + g*lp        HIGHPASS  hp
 *     MID    PEAK   in + g*bp        NOTCH     in - g*bp
 *     HIGH   SHELF  in + g*hp        LOWPASS   lp
 *
 * The low band's pass is a HIGH-pass and the high band's is a LOW-pass, which
 * reads backwards written down and is obvious in use: the low band owns the
 * bottom of the spectrum, so its pass mode is the one that removes the bottom.
 *
 * Four controls per band -- FREQUENCY, GAIN, RESONANCE, MIX -- which is one
 * Page, with SELECT choosing which band the knobs are driving.
 *
 * Why per band rather than per EQ: a mix control on a single band is parallel
 * EQ. Blending a hard notch to 50% is a different, softer thing than a gentle
 * notch, and resonance on a shelf is the difference between a tone control
 * and a character one. These are the controls that make three bands enough.
 *
 * This header is plain C -- fm1_svf.h replaced the braids::Svf an earlier
 * version used, so dsp/tests can exercise it directly.
 */
#ifndef FM1_EQ_H
#define FM1_EQ_H

#include "../shared/fm1_fixed.h"
#include "fm1_svf.h"

/* Band identity. Fixed roles, not three interchangeable bands: the mode list
 * differs per band, which is what keeps the whole EQ to one enum per band
 * instead of a type menu. */
enum { EQ_LOW = 0, EQ_MID, EQ_HIGH, EQ_BANDS };

/* Mode is one bit per band; what it means depends on which band. */
enum { EQB_SHELF = 0, EQB_PASS = 1 };   /* LOW and HIGH */
enum { EQB_PEAK  = 0, EQB_NOTCH = 1 };  /* MID */

static const char *const FM1_EQ_MODE_NAME[EQ_BANDS][2] = {
    { "SHELF", "HIGHPASS" },
    { "PEAK",  "NOTCH"    },
    { "SHELF", "LOWPASS"  },
};

typedef struct {
    fm1_svf_t svf;
    uint8_t   mode;         /* 0 or 1, meaning per band */
    uint8_t   mix;          /* 0..127 dry/wet for this band alone */
    int32_t   gain;         /* Q12, bipolar; 0 is flat */
} fm1_eq_band_t;

typedef struct {
    fm1_eq_band_t band[EQ_BANDS];
} fm1_eq_t;

/* Band gain from a 0..127 knob centred at 64.
 *
 * Asymmetric on purpose. Boost runs to +3x of the extracted band, roughly
 * +12 dB; cut runs to -1x, which REMOVES the band rather than merely
 * reducing it. A symmetric -3x would be a polarity inversion of the band and
 * sounds like a wiring fault, not like an EQ. */
static inline int32_t fm1_eq_gain(uint8_t knob)
{
    int32_t k = (int32_t)knob - 64;
    if (k == 0) return 0;
    if (k < 0)  return (k * Q12_ONE) / 64;              /* down to -1.0 */
    return (k * (3 * Q12_ONE)) / 63;                    /* up to +3.0 */
}

static inline void fm1_eq_init(fm1_eq_t *e)
{
    static const uint8_t kDefaultNote[EQ_BANDS] = { 36, 72, 96 };
    for (int b = 0; b < EQ_BANDS; b++) {
        fm1_svf_init(&e->band[b].svf);
        fm1_svf_set_note(&e->band[b].svf, kDefaultNote[b]);
        fm1_svf_set_res(&e->band[b].svf, 40);
        e->band[b].mode = 0;
        e->band[b].mix  = 127;
        e->band[b].gain = 0;
    }
}

static inline void fm1_eq_set_freq(fm1_eq_t *e, uint8_t b, uint8_t v)
{ if (b < EQ_BANDS) fm1_svf_set_note(&e->band[b].svf, v); }

static inline void fm1_eq_set_gain(fm1_eq_t *e, uint8_t b, uint8_t v)
{ if (b < EQ_BANDS) e->band[b].gain = fm1_eq_gain(v); }

static inline void fm1_eq_set_res(fm1_eq_t *e, uint8_t b, uint8_t v)
{ if (b < EQ_BANDS) fm1_svf_set_res(&e->band[b].svf, v); }

static inline void fm1_eq_set_mix(fm1_eq_t *e, uint8_t b, uint8_t v)
{ if (b < EQ_BANDS) e->band[b].mix = v; }

static inline void fm1_eq_set_mode(fm1_eq_t *e, uint8_t b, uint8_t v)
{ if (b < EQ_BANDS) e->band[b].mode = v ? 1u : 0u; }

/* True when a band cannot change the signal, so it can be skipped entirely.
 *
 * A shelf or bell at unity gain is a no-op; a pass mode never is, because a
 * pass replaces the signal rather than adding to it. Most voices sit flat on
 * most bands, and an EQ that taxes every voice for bands nobody touched is a
 * tax on the whole instrument. */
static inline int fm1_eq_band_idle(const fm1_eq_band_t *b, uint8_t which)
{
    int is_pass = (which == EQ_MID) ? 0 : (b->mode == EQB_PASS);
    return !is_pass && b->gain == 0;
}

/* True when NO band can change the signal, so the whole EQ can be skipped.
 *
 * Asked once per block by the caller, not once per sample. Leaving the test
 * inside the sample loop cost 39,378 cycles a block -- 2.8% of the audio
 * budget -- for an EQ that was entirely flat, which is how most voices sit
 * most of the time. Band settings change at knob rate, so hoisting it is
 * exact rather than an approximation. */
static inline int fm1_eq_idle(const fm1_eq_t *e)
{
    for (uint8_t b = 0; b < EQ_BANDS; b++)
        if (!fm1_eq_band_idle(&e->band[b], b)) return 0;
    return 1;
}

static inline q15_t fm1_eq_tick(fm1_eq_t *e, q15_t x)
{
    int32_t s = x;

    for (uint8_t b = 0; b < EQ_BANDS; b++) {
        fm1_eq_band_t *bd = &e->band[b];
        if (fm1_eq_band_idle(bd, b)) continue;

        fm1_svf_out_t o = fm1_svf_tick(&bd->svf, s);
        int32_t wet;

        switch (b) {
        case EQ_LOW:
            wet = (bd->mode == EQB_PASS) ? o.hp
                                         : s + ((o.lp * bd->gain) >> Q12_BITS);
            break;
        case EQ_HIGH:
            wet = (bd->mode == EQB_PASS) ? o.lp
                                         : s + ((o.hp * bd->gain) >> Q12_BITS);
            break;
        default:    /* EQ_MID */
            wet = (bd->mode == EQB_NOTCH)
                      ? s - ((o.bp * bd->gain) >> Q12_BITS)
                      : s + ((o.bp * bd->gain) >> Q12_BITS);
            break;
        }

        s = (bd->mix >= 127) ? wet
                             : s + (((wet - s) * (int32_t)bd->mix) >> 7);
        s = fm1_q31_to_q15_sat(s);
    }

    return (q15_t)s;
}

/* --- drive ----------------------------------------------------------------
 *
 * The cubic saturator this stage uses now lives in fm1_fixed.h, beside the
 * master limiter it is deliberately different from. It moved when the voice
 * insert slots gained a DRIVE type: two callers in two headers wanting the
 * same curve means the curve is not an EQ detail.
 */

/* DRIVE knob 0..127 -> pre-gain 1x..8x. 0 bypasses the saturator entirely. */
static inline int32_t fm1_drive_gain(uint8_t v)
{
    return Q12_ONE + ((int32_t)v * (7 * Q12_ONE)) / 127;
}

#endif /* FM1_EQ_H */
