/* SPDX-License-Identifier: MIT
 *
 * The parameter table for the drone engine.
 *
 * One table drives the screen label, the range, the default, the display
 * formatting and the MIDI CC number. Nothing is addressable in two places, so
 * the CC map, the UI and the patch format cannot drift apart.
 *
 * The shape is taken from obervave's obv_params.h (GPL-3.0, zvenson) -- the
 * IDEA of a pdef_t table with auto-assigned CCs. No code is copied; this file
 * is written from scratch and is MIT, so the engine stays reusable. See
 * docs/LICENSE_REGISTER.md.
 *
 * Layout and CC assignments: docs/MIDI_SPEC.md.
 */
#ifndef FM1_PARAMS_H
#define FM1_PARAMS_H

#include <stdint.h>

/* --- per-voice parameters (MIDI channels 1-4) --------------------------- */
/* The first six mirror Braids' own panel, minus FINE and COARSE: pitch comes
 * from the keys and OCTAVE here, so a tuning knob would be redundant.
 *
 * On the module FM and MODULATION are ATTENUVERTERS for CV inputs -- FM
 * scales a pitch CV, MODULATION scales a TIMBRE CV. The FM-1 has no CV jacks,
 * but it has an internal modulation source: the slow drift. So those two
 * knobs set how deeply the drift reaches pitch and timbre, keeping the
 * module's layout and meaning without the jacks. Both are bipolar with 64 as
 * no modulation, as an attenuverter's centre detent is. */
enum {
    P_SHAPE, P_TIMBRE, P_COLOR,
    P_FM,                   /* drift -> pitch,  bipolar, 64 = none */
    P_MOD,                  /* drift -> timbre, bipolar, 64 = none */
    P_DRIFT_RATE,           /* how fast the drift wanders, shared by both */
    P_OCTAVE, P_DETUNE, P_GLIDE,
    P_COMB_TUNE, P_COMB_FB, P_COMB_MIX,
    P_LEVEL, P_PAN,
    NPARAM
};

/* --- global parameters (MIDI channel 16) -------------------------------- */
enum {
    G_MASTER, G_SPREAD, G_BINAURAL, G_HAAS, G_DRIFT_SYNC,
    NGPARAM
};

/* How a value is shown. The engine ignores this; the firmware's UI uses it. */
enum {
    K_NUM,      /* 0..max as a number */
    K_BIP,      /* bipolar around the centre */
    K_ONOFF,
    K_SHAPE,    /* a Braids shape name */
    K_PCT,
    K_CENTS,
    K_RATIO
};

typedef struct {
    const char *name;   /* 8 characters at most: the screen label */
    uint8_t     max;    /* inclusive */
    uint8_t     def;
    uint8_t     kind;
    uint8_t     cc;     /* a fixed CC, or 0 to take the next from CC_AUTO_BASE */
} fm1_pdef_t;

/* Parameters with cc == 0 are assigned CC_AUTO_BASE, +1, +2 ... in table
 * order. Pinned CCs are the General MIDI conventions so a generic controller
 * and Ableton's own mappings do the expected thing. */
#define CC_AUTO_BASE 20

static const fm1_pdef_t FM1_PDEF[NPARAM] = {
    /*               name       max  def  kind      cc */
    [P_SHAPE]      = {"SHAPE",   47,   0, K_SHAPE,   0},
    [P_TIMBRE]     = {"TIMBRE", 127,  64, K_NUM,     0},
    [P_COLOR]      = {"COLOR",  127,  64, K_NUM,     0},
    /* 64 = centre = no modulation, as an attenuverter's detent. FM defaults
     * a little open so the instrument breathes out of the box; MOD defaults
     * closed because on the TRIPLE_*, FM and wavetable shapes TIMBRE is a
     * quantised table and modulating it steps rather than glides. */
    [P_FM]         = {"FM",     127,  80, K_BIP,     0},
    [P_MOD]        = {"MOD",    127,  64, K_BIP,     0},
    [P_DRIFT_RATE] = {"DRIFTRT",127,  20, K_NUM,     0},
    [P_OCTAVE]     = {"OCTAVE",   8,   4, K_NUM,     0},
    [P_DETUNE]     = {"DETUNE", 127,  64, K_CENTS,   0},
    [P_GLIDE]      = {"GLIDE",  127,   0, K_NUM,     0},
    [P_COMB_TUNE]  = {"CMBTUNE",127,  64, K_RATIO,   0},
    [P_COMB_FB]    = {"CMBFB",  127,   0, K_NUM,     0},
    [P_COMB_MIX]   = {"CMBMIX", 127,   0, K_PCT,     0},
    [P_LEVEL]      = {"LEVEL",  127, 100, K_NUM,     7},   /* GM volume */
    [P_PAN]        = {"PAN",    127,  64, K_BIP,    10},   /* GM pan */
};

static const fm1_pdef_t FM1_GDEF[NGPARAM] = {
    [G_MASTER]     = {"MASTER", 127, 100, K_NUM,     7},
    [G_SPREAD]     = {"SPREAD", 127,   0, K_NUM,     0},
    [G_BINAURAL]   = {"BINAURL",127,   0, K_NUM,     0},
    [G_HAAS]       = {"HAAS",   127,   0, K_NUM,     0},
    [G_DRIFT_SYNC] = {"DRFTSYN",  1,   0, K_ONOFF,   0},
};

/* CC -> parameter index + 1, 0 meaning unassigned. Built once at startup so
 * MIDI handling is a single array read per message. */
typedef struct {
    uint8_t voice[128];     /* channels 1-4 */
    uint8_t global[128];    /* channel 16 */
} fm1_ccmap_t;

static inline void fm1_ccmap_build(fm1_ccmap_t *m)
{
    uint8_t i, next = CC_AUTO_BASE;

    for (i = 0; i < 128; i++) { m->voice[i] = 0; m->global[i] = 0; }

    for (i = 0; i < NPARAM; i++) {
        uint8_t cc = FM1_PDEF[i].cc;
        if (cc == 0) {
            /* skip any CC already pinned by another parameter */
            while (next < 128 && m->voice[next]) next++;
            cc = next++;
        }
        if (cc < 128) m->voice[cc] = (uint8_t)(i + 1);
    }

    next = CC_AUTO_BASE;
    for (i = 0; i < NGPARAM; i++) {
        uint8_t cc = FM1_GDEF[i].cc;
        if (cc == 0) {
            while (next < 128 && m->global[next]) next++;
            cc = next++;
        }
        if (cc < 128) m->global[cc] = (uint8_t)(i + 1);
    }
}

#endif /* FM1_PARAMS_H */
