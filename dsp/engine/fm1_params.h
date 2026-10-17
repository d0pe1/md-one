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
    /* Two multimode filters: OSC -> F1 -> VCA -> F2 -> pan. Both run the same
     * mode list (fm1_filter.h), which includes COMB and RESO -- so the three
     * old CMBTUNE/CMBFB/CMBMIX parameters are gone, folded into a filter that
     * happens to be in comb mode. Four parameters each, which is exactly one
     * Page of the UX taxonomy. */
    P_F1_TYPE, P_F1_CUT, P_F1_RES, P_F1_MIX,
    P_F2_TYPE, P_F2_CUT, P_F2_RES, P_F2_MIX,
    P_LEVEL, P_PAN,
    /* Collapse this voice to centre regardless of PAN. A mono-compatibility
     * check you can hit while the DAW is recording -- and what the unison and
     * Haas work will need once a voice can be genuinely stereo. */
    P_MONO,
    /* Where this voice's output goes: DEST_OFF, DEST_FX1..FX4, DEST_OUT.
     * A voice routes exactly the way an effect channel does -- one value, one
     * cable -- which is what keeps the routing model a single idea instead of
     * two (fm1_rack.h). */
    P_DEST,
    NPARAM
};

/* --- global parameters (MIDI channel 16) -------------------------------- */
enum {
    G_MASTER, G_SPREAD, G_BINAURAL, G_HAAS, G_DRIFT_SYNC,
    /* The FX patchbay: four effect channels (fm1_rack.h). Five controls
     * each -- TYPE, two effect parameters, MIX, and DEST, the one cable out.
     * DEST is what makes this a patchbay rather than an insert rack: set it
     * to OFF and everything feeding that channel goes silent, because the
     * cable is unplugged. */
    G_FX1_TYPE, G_FX1_P1, G_FX1_P2, G_FX1_MIX, G_FX1_DEST,
    G_FX2_TYPE, G_FX2_P1, G_FX2_P2, G_FX2_MIX, G_FX2_DEST,
    G_FX3_TYPE, G_FX3_P1, G_FX3_P2, G_FX3_MIX, G_FX3_DEST,
    G_FX4_TYPE, G_FX4_P1, G_FX4_P2, G_FX4_MIX, G_FX4_DEST,
    NGPARAM
};

/* Channel n (0-based) parameter j. The five-per-channel stride is relied on
 * by the engine's SetGlobal and by any UI that walks the patchbay. */
#define G_FX_BASE    G_FX1_TYPE
#define G_FX_STRIDE  5
#define G_FX(n, j)   (G_FX_BASE + (n) * G_FX_STRIDE + (j))
#define G_FX_TYPE    0
#define G_FX_P1      1
#define G_FX_P2      2
#define G_FX_MIX     3
#define G_FX_DEST    4

/* How a value is shown. The engine ignores this; the firmware's UI uses it. */
enum {
    K_NUM,      /* 0..max as a number */
    K_BIP,      /* bipolar around the centre */
    K_ONOFF,
    K_SHAPE,    /* a Braids shape name */
    K_PCT,
    K_CENTS,
    K_RATIO,
    K_FTYPE,    /* a filter mode name, FM1_FMODE_NAME in fm1_filter.h */
    K_FXTYPE,   /* an effect name,     FM1_FXNAME   in fm1_rack.h */
    K_DEST      /* a routing target,  FM1_DESTNAME in fm1_rack.h */
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
    /* Both filters default to OFF, which is a true bypass. A patch that wants
     * the old comb character sets F1 TYPE to COMB explicitly -- the previous
     * default was a comb at mix 0, which was the same silence reached by a
     * more expensive route. CUT is centred because its meaning is per mode:
     * absolute cutoff for LP/BP/HP, a semitone offset for RESO, a delay ratio
     * for COMB, and 64 is the neutral reading of all three. */
    /* max is F_NMODES-1 and def is F_OFF, written as literals so this header
     * stays free of fm1_filter.h -- which would drag braids::Svf in and make
     * it C++ only. fm1_drone.cc static_asserts that they still agree. */
    [P_F1_TYPE]    = {"F1 TYPE",  5,   0, K_FTYPE,   0},
    [P_F1_CUT]     = {"F1 CUT", 127,  64, K_NUM,     74},  /* GM brightness */
    [P_F1_RES]     = {"F1 RES", 127,   0, K_NUM,     71},  /* GM harmonic content */
    [P_F1_MIX]     = {"F1 MIX", 127, 127, K_PCT,     0},
    [P_F2_TYPE]    = {"F2 TYPE",  5,   0, K_FTYPE,   0},
    [P_F2_CUT]     = {"F2 CUT", 127,  64, K_NUM,     0},
    [P_F2_RES]     = {"F2 RES", 127,   0, K_NUM,     0},
    [P_F2_MIX]     = {"F2 MIX", 127, 127, K_PCT,     0},
    [P_LEVEL]      = {"LEVEL",  127, 100, K_NUM,     7},   /* GM volume */
    [P_PAN]        = {"PAN",    127,  64, K_BIP,    10},   /* GM pan */
    [P_MONO]       = {"MONO",     1,   0, K_ONOFF,   0},
    /* Defaults straight to OUT: a new voice is audible. Having to patch a
     * cable before anything is heard is correct for a modular and wrong for
     * an instrument you just switched on. max is DEST_NDEST-1 as a literal,
     * checked against the enum in fm1_drone.cc. */
    [P_DEST]       = {"DEST",     5,   5, K_DEST,    0},
};

static const fm1_pdef_t FM1_GDEF[NGPARAM] = {
    [G_MASTER]     = {"MASTER", 127, 100, K_NUM,     7},
    [G_SPREAD]     = {"SPREAD", 127,   0, K_NUM,     0},
    [G_BINAURAL]   = {"BINAURL",127,   0, K_NUM,     0},
    [G_HAAS]       = {"HAAS",   127,   0, K_NUM,     0},
    [G_DRIFT_SYNC] = {"DRFTSYN",  1,   0, K_ONOFF,   0},

    /* Effect channels. max for TYPE is FX_NTYPES-1 and for OUT is
     * DEST_NDEST-1, written as literals so this header stays free of
     * fm1_rack.h; fm1_drone.cc static_asserts both. Every channel starts
     * unplugged (DEST OFF), so a patch opts into the FX rather than having
     * to switch them off. */
    [G_FX1_TYPE]   = {"FX1 TYP",  5,   0, K_FXTYPE,  0},
    [G_FX1_P1]     = {"FX1 P1", 127,  64, K_NUM,     0},
    [G_FX1_P2]     = {"FX1 P2", 127,  64, K_NUM,     0},
    [G_FX1_MIX]    = {"FX1 MIX",127, 127, K_PCT,     0},
    [G_FX1_DEST]   = {"FX1 OUT",  5,   0, K_DEST,    0},
    [G_FX2_TYPE]   = {"FX2 TYP",  5,   0, K_FXTYPE,  0},
    [G_FX2_P1]     = {"FX2 P1", 127,  64, K_NUM,     0},
    [G_FX2_P2]     = {"FX2 P2", 127,  64, K_NUM,     0},
    [G_FX2_MIX]    = {"FX2 MIX",127, 127, K_PCT,     0},
    [G_FX2_DEST]   = {"FX2 OUT",  5,   0, K_DEST,    0},
    [G_FX3_TYPE]   = {"FX3 TYP",  5,   0, K_FXTYPE,  0},
    [G_FX3_P1]     = {"FX3 P1", 127,  64, K_NUM,     0},
    [G_FX3_P2]     = {"FX3 P2", 127,  64, K_NUM,     0},
    [G_FX3_MIX]    = {"FX3 MIX",127, 127, K_PCT,     0},
    [G_FX3_DEST]   = {"FX3 OUT",  5,   0, K_DEST,    0},
    [G_FX4_TYPE]   = {"FX4 TYP",  5,   0, K_FXTYPE,  0},
    [G_FX4_P1]     = {"FX4 P1", 127,  64, K_NUM,     0},
    [G_FX4_P2]     = {"FX4 P2", 127,  64, K_NUM,     0},
    [G_FX4_MIX]    = {"FX4 MIX",127, 127, K_PCT,     0},
    [G_FX4_DEST]   = {"FX4 OUT",  5,   0, K_DEST,    0},
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
