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
    /* Channel EQ (fm1_eq.h): three multimode bands, five controls each --
     * MODE, FREQ, GAIN, RES, MIX. One Page of four knobs plus the mode on
     * ALGORITHM, with SELECT choosing which band the knobs drive. */
    P_EQ1_MODE, P_EQ1_FRQ, P_EQ1_GAIN, P_EQ1_RES, P_EQ1_MIX,
    P_EQ2_MODE, P_EQ2_FRQ, P_EQ2_GAIN, P_EQ2_RES, P_EQ2_MIX,
    P_EQ3_MODE, P_EQ3_FRQ, P_EQ3_GAIN, P_EQ3_RES, P_EQ3_MIX,
    P_DRIVE,
    /* VCA envelope (fm1_env.h). Not an ADSR: voices are latched, so sustain
     * is the normal state. Both reach about 32 s, because a drone arriving
     * over twenty seconds is an ordinary thing to want here. */
    P_ATTACK, P_RELEASE,
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

/* EQ band b (0..2), control j. Same shape as the patchbay's G_FX(). */
#define P_EQ_BASE    P_EQ1_MODE
#define P_EQ_STRIDE  5
#define P_EQ(b, j)   (P_EQ_BASE + (b) * P_EQ_STRIDE + (j))
#define P_EQ_MODE    0
#define P_EQ_FRQ     1
#define P_EQ_GAIN    2
#define P_EQ_RES     3
#define P_EQ_MIX     4

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

/* --- mixer meter ladder ---------------------------------------------------
 *
 * Nine segments, lit from the bottom when Meter() reaches each threshold.
 * The engine ignores this; the firmware's UI draws it.
 *
 * Spacing is roughly logarithmic -- 6 dB per step up to -12, then 3 dB -- so
 * the useful top of the range gets the resolution. A linear ladder puts seven
 * of the nine segments below -18 dB, where nothing worth seeing happens.
 *
 *   segment   1   2   3   4   5    6    7    8    9
 *   dBFS    -36 -30 -24 -18 -12   -9   -6   -3    0
 *   colour  <------- green ------> <----- blue ----->
 *
 * All nine turn RED while Clipping() holds, rather than only the top one: an
 * overload should be readable from across a room, not found by inspecting the
 * last segment.
 */
#define FM1_METER_SEGS   9
#define FM1_METER_GREEN  5      /* segments 1..5 green, 6..9 blue */

static const uint8_t FM1_METER_STEPS[FM1_METER_SEGS] = {
    2, 4, 8, 16, 32, 45, 64, 90, 127
};

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
    K_DEST,     /* a routing target,  FM1_DESTNAME in fm1_rack.h */
    K_EQMODE    /* an EQ band mode, FM1_EQ_MODE_NAME in fm1_eq.h */
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
    /* Three EQ bands. GAIN is centred (flat); MODE 0 is shelf/peak and 1 is
     * the pass or notch. Defaults put the bands at roughly 65 Hz, 523 Hz and
     * 2 kHz -- low, mid, air -- so the page is useful before anything is
     * moved. */
    [P_EQ1_MODE]  = {"EQ1 MOD",  1,   0, K_EQMODE,  0},
    [P_EQ1_FRQ]   = {"EQ1 FRQ",127,  36, K_NUM,     0},
    [P_EQ1_GAIN]  = {"EQ1 GAIN",127,  64, K_BIP,     0},
    [P_EQ1_RES]   = {"EQ1 RES",127,  40, K_NUM,     0},
    [P_EQ1_MIX]   = {"EQ1 MIX",127, 127, K_PCT,     0},
    [P_EQ2_MODE]  = {"EQ2 MOD",  1,   0, K_EQMODE,  0},
    [P_EQ2_FRQ]   = {"EQ2 FRQ",127,  72, K_NUM,     0},
    [P_EQ2_GAIN]  = {"EQ2 GAIN",127,  64, K_BIP,     0},
    [P_EQ2_RES]   = {"EQ2 RES",127,  40, K_NUM,     0},
    [P_EQ2_MIX]   = {"EQ2 MIX",127, 127, K_PCT,     0},
    [P_EQ3_MODE]  = {"EQ3 MOD",  1,   0, K_EQMODE,  0},
    [P_EQ3_FRQ]   = {"EQ3 FRQ",127,  96, K_NUM,     0},
    [P_EQ3_GAIN]  = {"EQ3 GAIN",127,  64, K_BIP,     0},
    [P_EQ3_RES]   = {"EQ3 RES",127,  40, K_NUM,     0},
    [P_EQ3_MIX]   = {"EQ3 MIX",127, 127, K_PCT,     0},
    [P_DRIVE]      = {"DRIVE",  127,   0, K_NUM,     0},
    /* Default to the fast end: an instrument you switch on should make a
     * sound when you press a key, not fade in politely. */
    [P_ATTACK]     = {"ATTACK", 127,   8, K_NUM,     0},
    [P_RELEASE]    = {"RELEASE",127,  20, K_NUM,     0},
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
