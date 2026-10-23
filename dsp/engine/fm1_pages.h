/* SPDX-License-Identifier: MIT
 *
 * The menu and page map -- what the knobs are wired to, and where you are.
 *
 * docs/UX_DESIGN.md defines the control surface; this is that document as
 * data. A Menu is a collection of Pages; a Page exposes exactly four
 * Parameters, one per knob, plus an optional enum on the ALGORITHM encoder.
 * Nothing here draws anything: rendering lives in the firmware, which is
 * GPL-3.0, and this header stays MIT so dsp/ remains reusable.
 *
 * The one rule worth stating up front, because every other decision follows
 * from it: a page NEVER borrows a knob for something else. Four knobs, four
 * parameters, and a page with fewer shows `--`. That is what makes the
 * surface learnable rather than memorised, and it is why the knob slots are
 * a fixed-size array rather than a list.
 */
#ifndef FM1_PAGES_H
#define FM1_PAGES_H

#include <stdint.h>

#include "fm1_params.h"

/* --- menus ---------------------------------------------------------------
 *
 * All nine of docs/UX_DESIGN.md section 4's menus, now that Phase 2 has
 * created the LFO and modulation parameters they navigate.
 */
enum { M_EDIT, M_ENV, M_LFO, M_MOD, M_FX, M_MIX, M_GLO, M_HOME, M_SAVE, M_NMENU };

/* Which table a page's parameter ids index. */
enum { SCOPE_VOICE, SCOPE_GLOBAL };

/* --- subjects ------------------------------------------------------------
 *
 * What the SELECT encoder is choosing. Section 3 invariant 3 says SELECT only
 * ever chooses the Subject and never edits a value -- that holds here, but
 * the Subject is a property of the PAGE rather than of the menu.
 *
 * The EQ page is why. It needs SELECT to choose which of three bands the
 * knobs drive, while the voice stays whatever it was ("VOICE 1 . BAND 2 MID"
 * in the mockup). Keeping one selector per subject KIND, remembered
 * independently, serves that without a special case: the EQ page selects a
 * band, every other EDIT page selects a voice, and both are still on screen
 * because both counters survive the page change.
 */
enum { SUBJ_NONE, SUBJ_VOICE, SUBJ_FX, SUBJ_BAND, SUBJ_LFO,
       SUBJ_SLOT, SUBJ_NKIND };

static const uint8_t FM1_SUBJ_COUNT[SUBJ_NKIND] = { 1, 4, 4, 3, 4, 2 };
static const char *const FM1_SUBJ_NAME[SUBJ_NKIND] = {
    "", "VOICE", "FX", "BAND", "LFO", "SLOT"
};

#define FM1_KNOBS  4
#define PG_NONE    (-1)   /* this knob is inactive on this page: show `--` */

typedef struct {
    const char *name;               /* page name, shown in the title row */
    int16_t     k[FM1_KNOBS];       /* KNOB1..4, or PG_NONE */
    /* What SHIFT + the same knob edits, or PG_NONE. Only the MOD pages use
     * it, and they are why it exists: a destination needs a DEPTH and a
     * SOURCE, which is eight controls on a four-knob page. Putting the
     * source on the modifier keeps both visible at once -- the routing IS
     * the page -- instead of making it a matrix to navigate.
     *
     * It does not break "SHIFT is only ever SHIFT" (invariant 4): SHIFT
     * still reveals a second function rather than having one of its own. */
    int16_t     ks[FM1_KNOBS];
    int16_t     alg;                /* the ALGORITHM encoder's enum, or PG_NONE */
    uint8_t     subject;            /* SUBJ_* -- what SELECT moves here */
    /* Parameter id step per subject. Zero means the subject picks which
     * INSTANCE the page acts on (voice 2's copy of P_TIMBRE), not which
     * parameter -- that is the common case. Non-zero means the subject picks
     * a different parameter within one instance, which is how one EQ page
     * serves three bands and one FX page serves four channels. */
    uint8_t     stride;
} fm1_page_t;

typedef struct {
    const char       *name;
    uint8_t           scope;        /* SCOPE_VOICE or SCOPE_GLOBAL */
    uint8_t           npages;
    const fm1_page_t *pages;
} fm1_menu_t;

/* --- EDIT: the oscillator and its voice ---------------------------------- */
static const fm1_page_t FM1_PG_EDIT[] = {
    { "BRAIDS", { P_TIMBRE, P_COLOR, P_FM, P_MOD },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      P_SHAPE, SUBJ_VOICE, 0 },

    /* DRIFT RATE sits on KNOB4 because it has nowhere else to be and belongs
     * here: FM and MOD on page 1 are attenuverters scaling the drift, and
     * this is the drift they scale. The UX document leaves this knob empty
     * and leaves the parameter homeless, which cannot both be right. */
    { "PITCH",  { P_OCTAVE, P_DETUNE, P_GLIDE, P_DRIFT_RATE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_VOICE, 0 },

    /* Both filters run the same mode list, so these two pages are identical
     * in shape. CUTOFF means what the mode needs it to mean -- an absolute
     * cutoff for LP/BP/HP, a semitone offset for RESO, a delay ratio for
     * COMB -- and the screen relabels it, because a knob called CUTOFF that
     * sets a delay ratio is a knob nobody trusts. */
    { "F1",     { P_F1_CUT, P_F1_RES, P_F1_MIX, P_F1_POLE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      P_F1_TYPE, SUBJ_VOICE, 0 },

    /* The insert slots, between the filters. SELECT picks which one, so two
     * stages share one page the way the three EQ bands do -- and the body
     * draws the chain with the selected stage marked, which is the part the
     * EQ page was missing. */
    { "SLOT",   { P_SL(0, P_SL_P1), P_SL(0, P_SL_P2),
                  P_SL(0, P_SL_MIX), PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      P_SL(0, P_SL_TYPE), SUBJ_SLOT, P_SL_STRIDE },

    { "F2",     { P_F2_CUT, P_F2_RES, P_F2_MIX, P_F2_POLE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      P_F2_TYPE, SUBJ_VOICE, 0 },

    /* One page, three bands, SELECT choosing between them -- the Serum idiom.
     * The stride is what makes that one page instead of three. */
    { "EQ",     { P_EQ(0, P_EQ_FRQ), P_EQ(0, P_EQ_GAIN),
                  P_EQ(0, P_EQ_RES), P_EQ(0, P_EQ_MIX) },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      P_EQ(0, P_EQ_MODE), SUBJ_BAND, P_EQ_STRIDE },

    /* Routing lives on the last page of the menu that owns the thing being
     * routed. One cable out, and `---` is unplugged. */
    { "ROUTE",  { P_DEST, P_LEVEL, P_PAN, P_MONO },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_VOICE, 0 },
};

/* --- ENV: AHDSR ----------------------------------------------------------
 *
 * HOLD is on page 2 because it is the least-reached of the five, not because
 * it matters least. A page with one knob on it is honest about that; the
 * alternative was cramming five controls onto four knobs, which invariant 1
 * forbids for good reason.
 */
static const fm1_page_t FM1_PG_ENV[] = {
    { "ADSR",   { P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_VOICE, 0 },
    { "HOLD",   { P_HOLD, P_ENV_MODE, PG_NONE, PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_VOICE, 0 },
};

/* --- FX: the patchbay's four effect channels -----------------------------
 *
 * There is no input control, and that is the point. `IN < V1 V2` on the
 * screen is derived by asking every other node where it points; routing TO a
 * channel is what fills it.
 */
static const fm1_page_t FM1_PG_FX[] = {
    { "EFFECT", { G_FX(0, G_FX_P1), G_FX(0, G_FX_P2),
                  G_FX(0, G_FX_MIX), PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      G_FX(0, G_FX_TYPE), SUBJ_FX, G_FX_STRIDE },
    { "ROUTE",  { G_FX(0, G_FX_DEST), PG_NONE, PG_NONE, PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_FX, G_FX_STRIDE },
};

/* --- LFO: four shaped modulators -----------------------------------------
 *
 * One LFO per page, chosen with SELECT -- the same Subject rule as a voice
 * or an FX channel, so there is nothing new to learn about navigating them.
 */
static const fm1_page_t FM1_PG_LFO[] = {
    { "SHAPE",  { G_LFO(0, G_LFO_RATE), G_LFO(0, G_LFO_AMT),
                  G_LFO(0, G_LFO_PHASE), PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      G_LFO(0, G_LFO_MODE), SUBJ_LFO, G_LFO_STRIDE },
};


/* --- MOD: where the LFOs reach ------------------------------------------
 *
 * A knob sets the DEPTH into a destination, bipolar. SHIFT + the same knob
 * picks the SOURCE. So each page shows four of the nine modulatable inputs
 * with their depth and source together -- the routing IS the page, rather
 * than a matrix to navigate to.
 *
 * Nine destinations rather than twelve because folding the comb into the
 * filter did it: COMB TUNE / FB / MIX were three of their own, and are now
 * the filter's CUT and RES, which had to be modulatable anyway.
 */
static const fm1_page_t FM1_PG_MOD[] = {
    { "MOD A",  { P_MD(MD_PITCH, P_MD_DEPTH), P_MD(MD_TIMBRE, P_MD_DEPTH),
                  P_MD(MD_COLOR, P_MD_DEPTH), P_MD(MD_LEVEL,  P_MD_DEPTH) },
                { P_MD(MD_PITCH, P_MD_SRC),   P_MD(MD_TIMBRE, P_MD_SRC),
                  P_MD(MD_COLOR, P_MD_SRC),   P_MD(MD_LEVEL,  P_MD_SRC)   },
      PG_NONE, SUBJ_VOICE, 0 },
    { "MOD B",  { P_MD(MD_F1CUT, P_MD_DEPTH), P_MD(MD_F2CUT, P_MD_DEPTH),
                  P_MD(MD_SL1,   P_MD_DEPTH), P_MD(MD_SL2,   P_MD_DEPTH) },
                { P_MD(MD_F1CUT, P_MD_SRC),   P_MD(MD_F2CUT, P_MD_SRC),
                  P_MD(MD_SL1,   P_MD_SRC),   P_MD(MD_SL2,   P_MD_SRC)   },
      PG_NONE, SUBJ_VOICE, 0 },
    { "MOD C",  { P_MD(MD_PAN, P_MD_DEPTH), PG_NONE, PG_NONE, PG_NONE },
                { P_MD(MD_PAN, P_MD_SRC),   PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_VOICE, 0 },
};

/* --- MIX: one page, all four channels visible ----------------------------
 *
 * A mixer spread across four pages is not a mixer. The four knobs follow the
 * selected strip and the red box around it IS the mapping -- it says "these
 * knobs belong to this strip" without a word of explanation.
 *
 * LEVEL, PAN and MONO also appear on EDIT/ROUTE. That duplication is
 * deliberate and costs nothing: a parameter is declared once in fm1_params.h,
 * so it has one CC and one patch byte no matter how many knobs reach it.
 */
static const fm1_page_t FM1_PG_MIX[] = {
    { "MIXER",  { P_LEVEL, P_PAN, P_MONO, P_DRIVE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_VOICE, 0 },
};

/* --- GLO: global ---------------------------------------------------------
 *
 * ONE page, not the two the UX document sketches. TRANSPOSE and FINE never
 * existed as parameters; BINAURAL and DRIFT SYNC did and were deleted once
 * it was clear the instrument already did both jobs better -- two detuned
 * voices panned apart beat against each other and the drift keeps that
 * moving, and coherent movement is what the LFOs are for. What is left fits
 * on one page with a knob to spare, and a menu with one page does not blink
 * its button.
 *
 * Stereo WIDTH is deliberately not here -- it is an effect type on an FX
 * channel, so it can sit anywhere in the patch rather than only at the end.
 */
static const fm1_page_t FM1_PG_GLO[] = {
    { "GLOBAL", { G_MASTER, G_SPREAD, G_HAAS, PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_NONE, 0 },
};

/* --- HOME and SAVE: no knobs --------------------------------------------- */
static const fm1_page_t FM1_PG_HOME[] = {
    { "HOME",   { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_NONE, 0 },
};
static const fm1_page_t FM1_PG_SAVE[] = {
    { "PATCH",  { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      { PG_NONE, PG_NONE, PG_NONE, PG_NONE },
      PG_NONE, SUBJ_NONE, 0 },
};

#define FM1_NPG(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const fm1_menu_t FM1_MENU[M_NMENU] = {
    [M_EDIT] = { "EDIT", SCOPE_VOICE,  FM1_NPG(FM1_PG_EDIT), FM1_PG_EDIT },
    [M_ENV]  = { "ENV",  SCOPE_VOICE,  FM1_NPG(FM1_PG_ENV),  FM1_PG_ENV  },
    [M_LFO]  = { "LFO",  SCOPE_GLOBAL, FM1_NPG(FM1_PG_LFO),  FM1_PG_LFO  },
    [M_MOD]  = { "MOD",  SCOPE_VOICE,  FM1_NPG(FM1_PG_MOD),  FM1_PG_MOD  },
    [M_FX]   = { "FX",   SCOPE_GLOBAL, FM1_NPG(FM1_PG_FX),   FM1_PG_FX   },
    [M_MIX]  = { "MIX",  SCOPE_VOICE,  FM1_NPG(FM1_PG_MIX),  FM1_PG_MIX  },
    [M_GLO]  = { "GLO",  SCOPE_GLOBAL, FM1_NPG(FM1_PG_GLO),  FM1_PG_GLO  },
    [M_HOME] = { "HOME", SCOPE_VOICE,  FM1_NPG(FM1_PG_HOME), FM1_PG_HOME },
    [M_SAVE] = { "SAVE", SCOPE_GLOBAL, FM1_NPG(FM1_PG_SAVE), FM1_PG_SAVE },
};

/* --- where you are -------------------------------------------------------
 *
 * One selector per subject kind, remembered independently, so moving from the
 * EQ page to the PITCH page does not disturb which band you were editing and
 * moving back does not disturb which voice.
 */
typedef struct {
    uint8_t menu;
    uint8_t page;
    uint8_t sel[SUBJ_NKIND];
} fm1_nav_t;

static inline void fm1_nav_init(fm1_nav_t *n)
{
    uint8_t i;
    n->menu = M_HOME;
    n->page = 0;
    for (i = 0; i < SUBJ_NKIND; i++) n->sel[i] = 0;
}

static inline const fm1_menu_t *fm1_nav_menu(const fm1_nav_t *n)
{
    return &FM1_MENU[n->menu];
}

static inline const fm1_page_t *fm1_nav_page(const fm1_nav_t *n)
{
    return &FM1_MENU[n->menu].pages[n->page];
}

/* A menu button was pressed.
 *
 * Invariant 5: the button enters its menu at page 1, and pressing it again
 * advances to the next page, wrapping at the end. SHIFT + the button returns
 * to page 1 from anywhere. One button therefore reaches every page of its own
 * menu, which is why there is no separate page control to find.
 */
static inline void fm1_nav_press(fm1_nav_t *n, uint8_t menu, int shift)
{
    if (menu >= M_NMENU) return;

    if (shift || n->menu != menu) {
        n->menu = menu;
        n->page = 0;
    } else {
        uint8_t np = FM1_MENU[menu].npages;
        n->page = (uint8_t)((n->page + 1) % (np ? np : 1));
    }
}

/* The SELECT encoder moved. Clamped, not wrapped: wrapping from voice 4 back
 * to voice 1 on a single nudge is a surprise while you are balancing, and the
 * panel already teaches clamping -- the OCT LEDs go dark at the limits. */
static inline void fm1_nav_select(fm1_nav_t *n, int delta)
{
    uint8_t kind = fm1_nav_page(n)->subject;
    int v;

    if (kind == SUBJ_NONE) return;

    v = (int)n->sel[kind] + delta;
    if (v < 0) v = 0;
    if (v >= (int)FM1_SUBJ_COUNT[kind]) v = FM1_SUBJ_COUNT[kind] - 1;
    n->sel[kind] = (uint8_t)v;
}

/* The parameter id under a knob, or PG_NONE. Index it into FM1_PDEF when the
 * menu's scope is SCOPE_VOICE, FM1_GDEF when it is SCOPE_GLOBAL. */
static inline int fm1_page_knob(const fm1_nav_t *n, uint8_t knob)
{
    const fm1_page_t *p = fm1_nav_page(n);
    int id;

    if (knob >= FM1_KNOBS) return PG_NONE;
    id = p->k[knob];
    if (id == PG_NONE) return PG_NONE;
    return id + (int)p->stride * (int)n->sel[p->subject];
}

/* What SHIFT + this knob edits, or PG_NONE. Same stride arithmetic as the
 * knob layer, so a shifted control on a strided page follows its subject. */
static inline int fm1_page_shift_knob(const fm1_nav_t *n, uint8_t knob)
{
    const fm1_page_t *p = fm1_nav_page(n);
    int id;

    if (knob >= FM1_KNOBS) return PG_NONE;
    id = p->ks[knob];
    if (id == PG_NONE) return PG_NONE;
    return id + (int)p->stride * (int)n->sel[p->subject];
}

/* The parameter id on the ALGORITHM encoder, or PG_NONE. */
static inline int fm1_page_alg(const fm1_nav_t *n)
{
    const fm1_page_t *p = fm1_nav_page(n);

    if (p->alg == PG_NONE) return PG_NONE;
    return p->alg + (int)p->stride * (int)n->sel[p->subject];
}

/* The definition behind a parameter id, picking the right table for the
 * current menu. Returns NULL for PG_NONE so a caller can draw `--`. */
static inline const fm1_pdef_t *fm1_page_def(const fm1_nav_t *n, int id)
{
    if (id == PG_NONE) return 0;
    if (FM1_MENU[n->menu].scope == SCOPE_GLOBAL) {
        return (id < NGPARAM) ? &FM1_GDEF[id] : 0;
    }
    return (id < NPARAM) ? &FM1_PDEF[id] : 0;
}

/* Does this menu have pages behind the one showing? Section 7: the menu
 * button blinks when it does, and that blink is the only hint a menu has
 * more than one page -- without it, pages beyond the first are invisible. */
static inline int fm1_menu_has_more(uint8_t menu)
{
    return menu < M_NMENU && FM1_MENU[menu].npages > 1;
}

/* --- fitting a label into a knob column ----------------------------------
 *
 * A column is 60 px and the small font runs about 9 px a character, so a
 * label has FM1_LABEL_MAX to work with. The screen's text box does not clip
 * -- a seventh character runs into the neighbouring column, which is exactly
 * what "EQ2 FRQEQ2 GAINEQ2 RES" looked like the first time this was built.
 *
 * Two rules, in order:
 *
 * 1. Drop everything up to the last space. The names in fm1_params.h are
 *    qualified -- "EQ2 FRQ", "F1 CUT", "FX1 P1" -- because they must be
 *    unique across 42 parameters and legible in a CC map. On screen the
 *    qualifier is already in the subject row ("BAND 2 MID", "FX 3"), so
 *    repeating it costs exactly the space the actual name needs.
 * 2. Abbreviate the handful still too long.
 *
 * This lives here rather than in the firmware so the fit can be TESTED. The
 * overlap above was found by taking a photograph of an emulated screen,
 * which is not a way to find things.
 */
#define FM1_LABEL_MAX 6

static const struct { const char *full; const char *brief; } FM1_ABBREV[] = {
    /* The envelope segments use the conventional three-letter set, the one
     * every synth panel has used for decades. Two reasons it is not a free
     * choice: it is what a player already reads without learning, and it has
     * to match the stage names drawn under the curve -- ATTACK on the knob
     * and ATK on the curve is two names for one segment on one page. */
    { "ATTACK",  "ATT"   },
    { "HOLD",    "HLD"   },
    { "DECAY",   "DEC"   },
    { "SUSTAIN", "SUS"   },
    { "RELEASE", "REL"   },

    { "DRIFTRT", "DRIFT" },
    { "BINAURL", "BINAU" },
    { "DRFTSYN", "SYNC"  },
};
#define FM1_NABBREV (sizeof FM1_ABBREV / sizeof FM1_ABBREV[0])

static inline int fm1_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* --- how far one detent goes ---------------------------------------------
 *
 * ONE DETENT IS ONE STEP. That is the rule, and it was not the rule before:
 * anything with a range over 16 moved by SIX per detent, so walking the 48
 * oscillator shapes skipped five at a time and a value on screen went 008,
 * 014, 020 while the knob clicked once, once, once. A control that does not
 * do what the click says is a control you cannot aim.
 *
 * The reason that multiplier existed is real, though: at one step per detent
 * a 0..127 parameter wants 127 clicks, which is six turns of the encoder. So
 * the range comes from ACCELERATION instead -- keep turning and the steps
 * grow, stop and the next click is worth exactly one again.
 *
 * Takes `now` rather than reading a clock, so it is pure and the firmware's
 * feel can be asserted on a host instead of photographed.
 *
 * HOW FAR it travels is set by SPEED, not by how long you have been turning.
 * The first version counted consecutive fast detents and multiplied by the
 * count, which is a different thing wearing the same name: past the 100 ms
 * threshold a brisk turn and a furious one moved a value at exactly the same
 * rate, and the only way to go faster was to keep going longer. On the stock
 * firmware -- and on every hardware encoder worth using -- spinning twice as
 * fast moves twice as far, immediately. That is what this computes.
 *
 * The law is quadratic in speed rather than linear:
 *
 *     v    = FM1_ACC_MS - dt        how far above the threshold you are
 *     mult = 1 + v*v / FM1_ACC_DIV
 *
 * which lands at 1 step the moment you cross back under the threshold, and
 * rises steeply once you are clearly travelling:
 *
 *     140 ms/detent   1 step      placing a value
 *     100 ms          2
 *      80 ms          4
 *      50 ms          8           a deliberate sweep: full range in ~16
 *      20 ms         13
 *      10 ms         15           flicked
 *
 * Linear-in-speed was tried on paper first and is too flat: to reach a useful
 * top it has to accelerate so early that placing a value gets hard, which is
 * the failure this whole function exists to avoid.
 */
#define FM1_ACC_MS   140u   /* slower than this and you are placing a value */
#define FM1_ACC_DIV  1600u  /* gain: 50 ms/detent gives 6 -- see the table */
#define FM1_ACC_MAX  12u    /* no detent is ever worth more than this */

typedef struct {
    uint32_t last_ms;
    uint16_t dt_avg;        /* smoothed interval between detents, ms */
    uint8_t  run;           /* consecutive fast detents */
} fm1_accel_t;

static inline int32_t fm1_enc_accel(fm1_accel_t *a, uint32_t now,
                                    int32_t detents, uint8_t max)
{
    uint32_t dt = now - a->last_ms;
    uint32_t n  = (uint32_t)(detents < 0 ? -detents : detents);
    uint32_t v, mult, ceil;

    a->last_ms = now;

    /* Small ranges never accelerate: a six-entry mode list has nowhere to go
     * and absolutely must not overshoot. */
    if (max <= 16u) { a->run = 0; return detents; }

    /* PER DETENT, not per call. fm1_enc_take() returns however many detents
     * piled up since the last poll, so a report of three means three clicks
     * happened inside this one interval -- the knob was turning three times
     * faster than `dt` alone says.
     *
     * Missing this inverted the whole control. Spin faster than the UI polls
     * and every extra detent lands in the SAME call, so the measured speed
     * saturates at the poll rate however hard you turn. Worse: a value change
     * forces a full redraw, so turning fast makes the poll interval LONGER,
     * and once it crosses FM1_ACC_MS a fast spin with ten queued detents
     * takes the "stopped" branch below and pays one step each. Turning harder
     * made the knob slower, which is exactly how it was reported from the
     * panel.
     *
     * The interval still includes any idle time before the burst began, so
     * this under-reads the speed rather than over-reading it. That is the
     * right direction to be wrong in. */
    if (n > 1u) dt /= n;

    if (dt >= FM1_ACC_MS) {             /* stopped, or never started */
        a->run = 0;
        a->dt_avg = FM1_ACC_MS;
        return detents;
    }

    /* Smoothed, because one mistimed detent should not launch the value
     * across the range. Two-tap IIR: each sample halves the error, so it
     * tracks a change of speed within three or four clicks and still ignores
     * a single outlier.
     *
     * But a report of several detents is ALREADY an average over that many
     * intervals, so it needs less smoothing, not the same -- and filtering it
     * at the same rate would measure the time constant in POLLS rather than
     * in clicks. At eight detents a poll that is a ramp lasting dozens of
     * detents, which is most of a fast spin spent not yet accelerating: the
     * residue of the same bug, one layer down.
     *
     * Less smoothing, though, is not NONE, and HOW MUCH less should follow
     * how many detents the report carries -- an average over eight is worth
     * more confidence than an average over two. So the weight toward the new
     * reading is n/4, capped:
     *
     *     n = 2   half way      a burst that might be two stray clicks
     *     n = 3   three quarters
     *     n >= 4  taken as read an unambiguous spin
     *
     * Taking EVERY burst as read was the previous version, and it jumped to
     * full speed on the first one -- from the panel that reads as coarse,
     * because the knob arrives rather than accelerates. Weighting it keeps
     * the gentle onset for the small bursts a responsive UI delivers, while
     * a genuinely fast spin still converges in one report. */
    if (a->dt_avg == 0u) a->dt_avg = (uint16_t)FM1_ACC_MS;
    if (n > 1u) {
        uint32_t w = n > 4u ? 4u : n;
        a->dt_avg = (uint16_t)(((4u - w) * (uint32_t)a->dt_avg + w * dt) >> 2);
    } else {
        a->dt_avg = (uint16_t)(((uint32_t)a->dt_avg + dt) >> 1);
    }

    /* The FIRST fast detent is still worth one step, so a quick two-tap to
     * nudge a value by two does not land on four. A report carrying several
     * detents at once is never a two-tap -- it is already a spin -- so it
     * clears this immediately instead of spending two whole polls, which at
     * five detents a poll would have cost ten clicks of ramp. */
    if (a->run < 2u) {
        a->run = (uint8_t)(a->run + n);
        if (a->run < 2u) return detents;
    }

    v = (uint32_t)a->dt_avg >= FM1_ACC_MS ? 0u
                                          : FM1_ACC_MS - (uint32_t)a->dt_avg;
    mult = 1u + (v * v) / FM1_ACC_DIV;

    /* A narrow parameter accelerates less than a wide one, so one detent can
     * never swallow a whole short list. A SIXTH of the range: on 0..127 this
     * never bites, and on the 48 shapes it caps at 8.
     *
     * An eighth, then a quarter, now a sixth -- and the middle one is the
     * only value that was never turned by a human. It was chosen on paper to
     * undo the eighth being too tight, overshot, and the shape list came back
     * from the panel as a slot machine.
     *
     * This was an EIGHTH, which capped the shapes at 6 where the old
     * run-length curve reached 9 -- so the one control most likely to be
     * spun hard got slower, not faster. A tighter cap made sense when a
     * sustained turn was the only way to travel; it does not now that slow
     * turns are exactly one step each, because the ceiling only ever applies
     * at full spin, which is precisely when travelling is what was asked
     * for. (Whole categories are still SHIFT + ALGORITHM.) */
    ceil = (uint32_t)max / 6u + 1u;
    if (ceil > FM1_ACC_MAX) ceil = FM1_ACC_MAX;
    if (mult > ceil) mult = ceil;

    return detents * (int32_t)mult;
}

static inline const char *fm1_label_fit(const char *name)
{
    const char *p = name, *last = name;
    uint32_t i;

    for (i = 0; i < FM1_NABBREV; i++)
        if (fm1_streq(name, FM1_ABBREV[i].full)) return FM1_ABBREV[i].brief;

    while (*p) { if (*p == ' ') last = p + 1; p++; }
    return last;
}

#endif /* FM1_PAGES_H */
