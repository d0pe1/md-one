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
    /* VCA envelope (fm1_env.h): AHDSR. Every time segment reaches about
     * 32 s, because a drone arriving over twenty seconds is an ordinary
     * thing to want here. HOLD is what gives a slow swell an audible peak
     * instead of a corner where the attack and decay curves meet. */
    P_ATTACK, P_HOLD, P_DECAY, P_SUSTAIN, P_RELEASE,
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
    /* The modulation matrix: nine destinations, each a DEPTH and a SOURCE.
     *
     * Nine rather than twelve because folding the comb into the filter did
     * it -- COMB TUNE / FB / MIX were three destinations of their own and
     * are now the filter's CUT, RES and MIX, which had to be modulatable
     * anyway. The routing IS the page: four destinations per page with their
     * depth and source visible together, rather than a matrix to navigate.
     *
     * DEPTH is bipolar with 64 as none, so a destination with no source and
     * a destination at zero depth are the same silence reached two ways. */
    P_MD0_DEPTH, P_MD0_SRC,     /* pitch   */
    P_MD1_DEPTH, P_MD1_SRC,     /* timbre  */
    P_MD2_DEPTH, P_MD2_SRC,     /* colour  */
    P_MD3_DEPTH, P_MD3_SRC,     /* level   */
    P_MD4_DEPTH, P_MD4_SRC,     /* F1 cut   */
    P_MD5_DEPTH, P_MD5_SRC,     /* F2 cut   */
    P_MD6_DEPTH, P_MD6_SRC,     /* slot 1 P1 */
    P_MD7_DEPTH, P_MD7_SRC,     /* slot 2 P1 */
    P_MD8_DEPTH, P_MD8_SRC,     /* pan     */

    /* Appended rather than slotted in beside the filters where they belong
     * logically, because every index below is mirrored as a literal in
     * fm1_drone_c.h and inserting would renumber thirty of them for a
     * tidiness nobody can see. The pages decide the order on screen. */

    /* SLOPE, one per filter: 6, 12 or 24 dB per octave. The fourth knob
     * these pages never had.
     *
     * Chosen over KEYTRACK, which was the first answer and the worse one: on
     * an instrument whose notes are LATCHED you set a pitch and leave it, so
     * a cutoff that follows the note earns very little -- while the step from
     * a gentle tilt to a wall is most of what a filter is for. */
    P_F1_POLE, P_F2_POLE,

    /* Two insert slots per voice, between the filters (fm1_slot.h). This is
     * where the comb and the resonator went when they stopped being filter
     * modes -- they can be stacked now, and driven into each other. */
    P_SL1_TYPE, P_SL1_P1, P_SL1_P2, P_SL1_MIX,
    P_SL2_TYPE, P_SL2_P1, P_SL2_P2, P_SL2_MIX,
    NPARAM
};

/* Slot n (0..1), control j. Same shape as P_EQ() and G_FX(). */
#define P_SL_BASE    P_SL1_TYPE
#define P_SL_STRIDE  4
#define P_SL(n, j)   (P_SL_BASE + (n) * P_SL_STRIDE + (j))
#define P_SL_TYPE    0
#define P_SL_P1      1
#define P_SL_P2      2
#define P_SL_MIX     3
#define FM1_SLOTS    2

/* Destination n (0-based), control j. Same shape as P_EQ() and G_FX(). */
#define P_MD_BASE    P_MD0_DEPTH
#define P_MD_STRIDE  2
#define P_MD(n, j)   (P_MD_BASE + (n) * P_MD_STRIDE + (j))
#define P_MD_DEPTH   0
#define P_MD_SRC     1

/* What each destination reaches. The order is the order the MOD pages walk
 * them, so a page is a contiguous run of four. */
/* The two filter RESONANCE destinations became the two SLOT destinations
 * when the comb and the resonator moved out of the filter. They had to: the
 * thing worth modulating on a comb is its TUNE, and after the move that
 * parameter was no longer reachable from the matrix at all -- which would
 * have silently broken the one scenario Phase 2 was declared done on, a slow
 * LFO sweeping a comb. Resonance is a character setting you place once;
 * tune is the one that wants to move. */
enum {
    MD_PITCH = 0, MD_TIMBRE, MD_COLOR, MD_LEVEL,
    MD_F1CUT, MD_F2CUT, MD_SL1, MD_SL2,
    MD_PAN,
    MD_NDEST
};

/* A source is none, one of the four LFOs, or the voice's own DRIFT.
 *
 * Drift is in the list because it was already a modulator with nowhere to
 * go: a per-voice random walk (fm1_drift.h) hardwired to exactly two
 * destinations, pitch and timbre, through the FM and MOD attenuverters that
 * Braids' own panel gives it. Those stay -- they are the module's layout --
 * but there was no reason the same signal could not also reach a filter or a
 * pan, which is all this does.
 *
 * It differs from an LFO in the way that matters: every voice has its OWN
 * drift, seeded differently, so routing drift to four voices gives four
 * unrelated movements where routing an LFO gives one shared movement. */
#define MD_SRC_NONE  0
#define MD_SRC_LFO1  1
#define MD_SRC_DRIFT 5
#define MD_SRC_COUNT 6
static const char *const FM1_MODSRC_NAME[MD_SRC_COUNT] = {
    "---", "LFO1", "LFO2", "LFO3", "LFO4", "DRIFT"
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
    G_MASTER, G_SPREAD, G_HAAS,
    /* The FX patchbay: four effect channels (fm1_rack.h). Five controls
     * each -- TYPE, two effect parameters, MIX, and DEST, the one cable out.
     * DEST is what makes this a patchbay rather than an insert rack: set it
     * to OFF and everything feeding that channel goes silent, because the
     * cable is unplugged. */
    G_FX1_TYPE, G_FX1_P1, G_FX1_P2, G_FX1_MIX, G_FX1_DEST,
    G_FX2_TYPE, G_FX2_P1, G_FX2_P2, G_FX2_MIX, G_FX2_DEST,
    G_FX3_TYPE, G_FX3_P1, G_FX3_P2, G_FX3_MIX, G_FX3_DEST,
    G_FX4_TYPE, G_FX4_P1, G_FX4_P2, G_FX4_MIX, G_FX4_DEST,
    /* Four LFOs, four controls each -- exactly one Page. Global rather than
     * per-voice on purpose: per-voice independence is already what the drift
     * gives, and what these add is the opposite, all four voices moving
     * together (fm1_lfo.h). */
    G_LFO1_RATE, G_LFO1_AMT, G_LFO1_PHASE, G_LFO1_MODE,
    G_LFO2_RATE, G_LFO2_AMT, G_LFO2_PHASE, G_LFO2_MODE,
    G_LFO3_RATE, G_LFO3_AMT, G_LFO3_PHASE, G_LFO3_MODE,
    G_LFO4_RATE, G_LFO4_AMT, G_LFO4_PHASE, G_LFO4_MODE,
    NGPARAM
};

/* LFO n (0-based), control j. Same shape as G_FX(). */
#define G_LFO_BASE   G_LFO1_RATE
#define G_LFO_STRIDE 4
#define G_LFO(n, j)  (G_LFO_BASE + (n) * G_LFO_STRIDE + (j))
#define G_LFO_RATE   0
#define G_LFO_AMT    1
#define G_LFO_PHASE  2
#define G_LFO_MODE   3

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
    K_CENTS,    /* cents, one per step, centre at def */
    K_OCT,      /* octave offset, shown signed around the centre */
    K_RATIO,
    K_FTYPE,    /* a filter mode name, FM1_FMODE_NAME in fm1_filter.h */
    K_FXTYPE,   /* an effect name,     FM1_FXNAME   in fm1_rack.h */
    K_DEST,     /* a routing target,  FM1_DESTNAME in fm1_rack.h */
    K_EQMODE,   /* an EQ band mode, FM1_EQ_MODE_NAME in fm1_eq.h */
    K_LFOMODE,  /* an LFO shape, FM1_LFO_MODE_NAME in fm1_lfo.h */
    K_MODSRC,   /* a modulation source, FM1_MODSRC_NAME above */
    K_SLOT,     /* an insert slot type, FM1_SLOT_NAME in fm1_slot.h */
    K_POLES     /* a filter slope, FM1_FPOLE_NAME in fm1_filter.h */
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
    /* OCTAVE is an OFFSET, so it is shown as one. As a plain 0..8 the centre
     * read "OCTAVE 004", which says nothing about being centred and makes
     * "down one" look like a move to 3 rather than to -1. */
    [P_OCTAVE]     = {"OCTAVE",   8,   4, K_OCT,     0},
    /* DETUNE counts CENTS, one per click, which is why max is 100 and not
     * 127. It used to be 0..127 displayed as -64..+63 and called cents while
     * the engine gave it +-50 -- so the number was 28% larger than the
     * detuning, and a click was 0.78 of the unit printed on the screen. */
    [P_DETUNE]     = {"DETUNE", 100,  50, K_CENTS,   0},
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
    [P_F1_TYPE]    = {"F1 TYPE",  4,   0, K_FTYPE,   0},
    [P_F1_CUT]     = {"F1 CUT", 127,  64, K_NUM,     74},  /* GM brightness */
    [P_F1_RES]     = {"F1 RES", 127,   0, K_NUM,     71},  /* GM harmonic content */
    [P_F1_MIX]     = {"F1 MIX", 127, 127, K_PCT,     0},
    [P_F2_TYPE]    = {"F2 TYPE",  4,   0, K_FTYPE,   0},
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
    /* Defaults are an organ: quick attack, no hold, full sustain, short
     * release. An instrument you switch on should make a sound when you
     * press a key, not fade in politely -- and SUSTAIN at full means DECAY
     * does nothing until you ask it to, so the envelope starts out of the
     * way. */
    [P_ATTACK]     = {"ATTACK", 127,   8, K_NUM,     0},
    [P_HOLD]       = {"HOLD",   127,   0, K_NUM,     0},
    [P_DECAY]      = {"DECAY",  127,  40, K_NUM,     0},
    [P_SUSTAIN]    = {"SUSTAIN",127, 127, K_NUM,     0},
    [P_RELEASE]    = {"RELEASE",127,  20, K_NUM,     0},
    [P_LEVEL]      = {"LEVEL",  127, 100, K_NUM,     7},   /* GM volume */
    [P_PAN]        = {"PAN",    127,  64, K_BIP,    10},   /* GM pan */
    [P_MONO]       = {"MONO",     1,   0, K_ONOFF,   0},
    /* Defaults straight to OUT: a new voice is audible. Having to patch a
     * cable before anything is heard is correct for a modular and wrong for
     * an instrument you just switched on. max is DEST_NDEST-1 as a literal,
     * checked against the enum in fm1_drone.cc. */
    [P_DEST]       = {"DEST",     5,   5, K_DEST,    0},

    /* Everything off: a new patch modulates nothing until asked. DEPTH is
     * bipolar so 64 is the centre detent, exactly as FM and MOD are on the
     * BRAIDS page -- the same gesture means the same thing. */
    [P_MD0_DEPTH] = {"PITCH",  127,  64, K_BIP,     0},
    [P_MD0_SRC]   = {"SRCPIT",   5,   0, K_MODSRC,  0},
    [P_MD1_DEPTH] = {"TIMBRE", 127,  64, K_BIP,     0},
    [P_MD1_SRC]   = {"SRCTMB",   5,   0, K_MODSRC,  0},
    [P_MD2_DEPTH] = {"COLOR",  127,  64, K_BIP,     0},
    [P_MD2_SRC]   = {"SRCCOL",   5,   0, K_MODSRC,  0},
    [P_MD3_DEPTH] = {"LEVEL",  127,  64, K_BIP,     0},
    [P_MD3_SRC]   = {"SRCLVL",   5,   0, K_MODSRC,  0},
    [P_MD4_DEPTH] = {"F1CUT",  127,  64, K_BIP,     0},
    [P_MD4_SRC]   = {"SRCF1C",   5,   0, K_MODSRC,  0},
    [P_MD5_DEPTH] = {"F2CUT",  127,  64, K_BIP,     0},
    [P_MD5_SRC]   = {"SRCF2C",   5,   0, K_MODSRC,  0},
    [P_MD6_DEPTH] = {"SLOT1",  127,  64, K_BIP,     0},
    [P_MD6_SRC]   = {"SRCSL1",   5,   0, K_MODSRC,  0},
    [P_MD7_DEPTH] = {"SLOT2",  127,  64, K_BIP,     0},
    [P_MD7_SRC]   = {"SRCSL2",   5,   0, K_MODSRC,  0},
    [P_MD8_DEPTH] = {"PAN",    127,  64, K_BIP,     0},
    [P_MD8_SRC]   = {"SRCPAN",   5,   0, K_MODSRC,  0},

    /* 12 dB is the default because it is what the filter was before the
     * knob existed -- no patch changes character by gaining a control. */
    [P_F1_POLE]    = {"F1 SLP",   2,   1, K_POLES,   0},
    [P_F2_POLE]    = {"F2 SLP",   2,   1, K_POLES,   0},

    /* Both slots start OFF, which is a true bypass rather than a wet/dry at
     * zero -- the same silence, reached without the arithmetic. P1 centres
     * because it is a tune or an offset on half the types. */
    [P_SL1_TYPE]   = {"SL1 TYP",  4,   0, K_SLOT,    0},
    [P_SL1_P1]     = {"SL1 P1", 127,  64, K_NUM,     0},
    [P_SL1_P2]     = {"SL1 P2", 127,   0, K_NUM,     0},
    [P_SL1_MIX]    = {"SL1 MIX",127, 127, K_PCT,     0},
    [P_SL2_TYPE]   = {"SL2 TYP",  4,   0, K_SLOT,    0},
    [P_SL2_P1]     = {"SL2 P1", 127,  64, K_NUM,     0},
    [P_SL2_P2]     = {"SL2 P2", 127,   0, K_NUM,     0},
    [P_SL2_MIX]    = {"SL2 MIX",127, 127, K_PCT,     0},
};

static const fm1_pdef_t FM1_GDEF[NGPARAM] = {
    [G_MASTER]     = {"MASTER", 127, 100, K_NUM,     7},
    [G_SPREAD]     = {"SPREAD", 127,   0, K_NUM,     0},
    /* Bipolar: 64 is no delay, and either side can be the early one. A
     * one-directional widener always pushes the image the same way, so
     * using it on two voices stacks them in the same place instead of
     * opening them apart. */
    [G_HAAS]       = {"HAAS",   127,  64, K_BIP,     0},

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

    /* AMOUNT defaults to 0: an LFO that is running but reaching nothing is
     * the right state to switch on in. RATE defaults slow -- 24 of 127 on an
     * exponential 0.01..20 Hz scale is about 0.1 Hz, a ten-second cycle,
     * which is what this instrument is for. PHASE spreads the four apart so
     * that two of them assigned to the same destination do not simply sum
     * into one deeper modulation. */
    [G_LFO1_RATE]  = {"LFO1 RT",127,  24, K_NUM,     0},
    [G_LFO1_AMT]   = {"LFO1 AM",127,   0, K_NUM,     0},
    [G_LFO1_PHASE] = {"LFO1 PH",127,   0, K_NUM,     0},
    [G_LFO1_MODE]  = {"LFO1 MD",  5,   0, K_LFOMODE, 0},
    [G_LFO2_RATE]  = {"LFO2 RT",127,  30, K_NUM,     0},
    [G_LFO2_AMT]   = {"LFO2 AM",127,   0, K_NUM,     0},
    [G_LFO2_PHASE] = {"LFO2 PH",127,  32, K_NUM,     0},
    [G_LFO2_MODE]  = {"LFO2 MD",  5,   0, K_LFOMODE, 0},
    [G_LFO3_RATE]  = {"LFO3 RT",127,  18, K_NUM,     0},
    [G_LFO3_AMT]   = {"LFO3 AM",127,   0, K_NUM,     0},
    [G_LFO3_PHASE] = {"LFO3 PH",127,  64, K_NUM,     0},
    [G_LFO3_MODE]  = {"LFO3 MD",  5,   5, K_LFOMODE, 0},   /* SMOOTH */
    [G_LFO4_RATE]  = {"LFO4 RT",127,  36, K_NUM,     0},
    [G_LFO4_AMT]   = {"LFO4 AM",127,   0, K_NUM,     0},
    [G_LFO4_PHASE] = {"LFO4 PH",127,  96, K_NUM,     0},
    [G_LFO4_MODE]  = {"LFO4 MD",  5,   0, K_LFOMODE, 0},
};

/* --- which parameters are slewed -----------------------------------------
 *
 * A 7-bit control is 128 steps, and on a sustained drone a step in anything
 * that multiplies the signal is a click. The engine slews those toward their
 * target at block rate instead of jumping (docs/ROADMAP.md, Phase 1).
 *
 * Mostly this follows from `kind`: an enum has nothing between its values, so
 * sliding through them would be meaningless -- a filter easing from COMB to
 * RESO is not a thing. Everything continuous is slewed.
 *
 * The exceptions are real rather than tidy:
 *
 *   OCTAVE       a button, and an octave should land when pressed. Sliding
 *                there is what GLIDE is for, and it is a separate control.
 *   A H D S R    times, not levels. There is nothing to zipper, and slewing
 *                a time means nothing. SUSTAIN is a level but the envelope
 *                already moves toward it at the decay rate -- it is slewed
 *                by construction, and slewing it twice would only add lag.
 *   DRIFT RATE   the drift wanders continuously anyway; a step in its speed
 *                is inaudible because the output is already smooth.
 */
static inline int fm1_param_is_smoothed(uint8_t p)
{
    if (p >= NPARAM) return 0;
    switch (p) {
    case P_OCTAVE: case P_DRIFT_RATE:
    case P_ATTACK: case P_HOLD: case P_DECAY: case P_SUSTAIN: case P_RELEASE:
        return 0;
    default:
        break;
    }
    switch (FM1_PDEF[p].kind) {
    case K_SHAPE: case K_ONOFF: case K_FTYPE:
    case K_FXTYPE: case K_DEST: case K_EQMODE: case K_MODSRC: case K_SLOT:
    case K_POLES:
        return 0;
    default:
        return 1;
    }
}

static inline int fm1_gparam_is_smoothed(uint8_t p)
{
    if (p >= NGPARAM) return 0;
    /* An LFO's RATE and PHASE move its oscillator, not the audio: slewing
     * them would make the knob feel laggy to no purpose, since the thing
     * they steer is already smooth. AMOUNT does scale a signal, so it is
     * slewed like any other gain. */
    switch (p) {
    case G_LFO1_RATE: case G_LFO1_PHASE:
    case G_LFO2_RATE: case G_LFO2_PHASE:
    case G_LFO3_RATE: case G_LFO3_PHASE:
    case G_LFO4_RATE: case G_LFO4_PHASE:
        return 0;
    default:
        break;
    }
    switch (FM1_GDEF[p].kind) {
    case K_SHAPE: case K_ONOFF: case K_FTYPE:
    case K_FXTYPE: case K_DEST: case K_EQMODE: case K_LFOMODE:
        return 0;
    default:
        return 1;
    }
}

/* How fast. The slew is a one-pole at block rate -- 172 blocks a second --
 * so a shift of 3 closes 1/8 of the remaining gap each block: 63% of the way
 * in 8 blocks, about 46 ms. Fast enough that a knob still feels direct, slow
 * enough that a 128th-of-range step stops being a click.
 *
 * A one-pole never actually ARRIVES, which matters twice over. The gap only
 * shrinks by 7/8 a block, so a full-scale jump needs 62 blocks -- 360 ms --
 * to land exactly, and for the last 36 of those the value is already within
 * one step of its target and audibly finished. Those blocks are pure waste:
 * every one of them calls back into the engine, and for a filter cutoff that
 * means recomputing a coefficient and the comb's tuning to move nothing.
 *
 * So the slew snaps as soon as the gap is under one whole step. The renderer
 * reads bytes; a gap smaller than that cannot change what it sees. */
#define FM1_SLEW_SHIFT 3
#define FM1_SLEW_SNAP  256      /* one step, in Q8 */

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
