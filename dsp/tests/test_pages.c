/* SPDX-License-Identifier: MIT
 *
 * Tests for the menu and page map.
 *
 * The one that matters is REACHABILITY. docs/ROADMAP.md says Phase 1 is done
 * when "every existing parameter is reachable through a page, nothing is
 * reachable any other way". That is a property of a data table, so it can be
 * a test rather than a promise -- and it is the test that fails the day
 * someone adds a parameter and forgets to give it a home. Without it, the
 * failure mode is a knob that exists in MIDI and nowhere on the device, which
 * nobody notices until they go looking for it.
 *
 * The rest check the invariants in docs/UX_DESIGN.md section 3, which are
 * stated there as rules the firmware MUST obey. A rule nothing enforces is a
 * comment.
 */
#include "../engine/fm1_pages.h"
#include "../engine/fm1_shapes.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int cond, const char *what, const char *detail)
{
    printf("  [%s] %s%s%s\n", cond ? "ok" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!cond) failures++;
}

/* Walk every page of every menu, at every subject value that page can take,
 * and mark each parameter the knobs and the ALGORITHM encoder can reach. */
static void sweep(uint8_t *voice_seen, uint8_t *global_seen)
{
    fm1_nav_t n;
    uint8_t m, pg, s, k;

    for (m = 0; m < M_NMENU; m++) {
        for (pg = 0; pg < FM1_MENU[m].npages; pg++) {
            uint8_t kind = FM1_MENU[m].pages[pg].subject;
            uint8_t nsub = FM1_SUBJ_COUNT[kind];

            for (s = 0; s < nsub; s++) {
                /* Knobs, SHIFT + knobs, and the ALGORITHM encoder. The
                 * shift layer is part of the sweep because it is part of
                 * what a human can reach: the MOD pages put a destination's
                 * depth on the knob and its source on the modifier, and a
                 * source nobody can get to is as broken as a knob nobody
                 * can turn. */
                int ids[FM1_KNOBS * 2 + 1];
                uint8_t i;

                fm1_nav_init(&n);
                n.menu = m;
                n.page = pg;
                n.sel[kind] = s;

                for (k = 0; k < FM1_KNOBS; k++) {
                    ids[k] = fm1_page_knob(&n, k);
                    ids[FM1_KNOBS + k] = fm1_page_shift_knob(&n, k);
                }
                ids[FM1_KNOBS * 2] = fm1_page_alg(&n);

                for (i = 0; i < FM1_KNOBS * 2 + 1; i++) {
                    if (ids[i] == PG_NONE) continue;
                    /* Per PAGE, not per menu. A page may edit global
                     * parameters inside a per-voice menu -- the mix buses
                     * are global and live on MIX, beside the voices they
                     * sum -- and scoring that page as voice-scope would
                     * both mark the wrong table and report the buses as
                     * unreachable. */
                    if (fm1_page_scope(&n) == SCOPE_GLOBAL) global_seen[ids[i]] = 1;
                    else                                    voice_seen[ids[i]] = 1;
                }
            }
        }
    }
}

static void test_reachable(void)
{
    uint8_t voice_seen[NPARAM], global_seen[NGPARAM];
    char detail[256];
    int i, missing = 0;

    memset(voice_seen, 0, sizeof voice_seen);
    memset(global_seen, 0, sizeof global_seen);
    sweep(voice_seen, global_seen);

    detail[0] = 0;
    for (i = 0; i < NPARAM; i++) {
        if (voice_seen[i]) continue;
        missing++;
        if (strlen(detail) < 200) {
            strcat(detail, FM1_PDEF[i].name);
            strcat(detail, " ");
        }
    }
    check(missing == 0, "every per-voice parameter is on some page", detail);

    detail[0] = 0;
    missing = 0;
    for (i = 0; i < NGPARAM; i++) {
        if (global_seen[i]) continue;
        missing++;
        if (strlen(detail) < 200) {
            strcat(detail, FM1_GDEF[i].name);
            strcat(detail, " ");
        }
    }
    check(missing == 0, "every global parameter is on some page", detail);
}

/* Invariant 1: four knobs, four parameters. A page never borrows a knob for
 * something else, so no parameter may appear twice ON ONE PAGE -- two knobs
 * editing the same value is the exact confusion the rule exists to prevent.
 *
 * Across pages is fine and deliberate: LEVEL/PAN/MONO sit on both EDIT/ROUTE
 * and MIX. A parameter is declared once in fm1_params.h, so it has one CC and
 * one patch byte however many knobs reach it. */
static void test_no_duplicate_on_page(void)
{
    fm1_nav_t n;
    uint8_t m, pg, a, b;
    int dupes = 0;

    for (m = 0; m < M_NMENU; m++) {
        for (pg = 0; pg < FM1_MENU[m].npages; pg++) {
            int ids[FM1_KNOBS];

            fm1_nav_init(&n);
            n.menu = m;
            n.page = pg;
            for (a = 0; a < FM1_KNOBS; a++) ids[a] = fm1_page_knob(&n, a);

            for (a = 0; a < FM1_KNOBS; a++) {
                if (ids[a] == PG_NONE) continue;
                for (b = (uint8_t)(a + 1); b < FM1_KNOBS; b++)
                    if (ids[a] == ids[b]) dupes++;
            }
        }
    }
    check(dupes == 0, "no parameter is on two knobs of the same page", 0);
}

/* Every id a page yields must resolve to a real definition -- the stride
 * arithmetic is the part that can silently run off the end of a table, and an
 * id past NPARAM would read whatever follows it in memory. */
static void test_ids_in_range(void)
{
    uint8_t voice_seen[NPARAM], global_seen[NGPARAM];
    fm1_nav_t n;
    uint8_t m, pg, s, k;
    int bad = 0, named = 0;

    memset(voice_seen, 0, sizeof voice_seen);
    memset(global_seen, 0, sizeof global_seen);
    sweep(voice_seen, global_seen);   /* would have crashed already if wild */

    for (m = 0; m < M_NMENU; m++) {
        for (pg = 0; pg < FM1_MENU[m].npages; pg++) {
            uint8_t kind = FM1_MENU[m].pages[pg].subject;

            for (s = 0; s < FM1_SUBJ_COUNT[kind]; s++) {
                fm1_nav_init(&n);
                n.menu = m;
                n.page = pg;
                n.sel[kind] = s;

                for (k = 0; k < FM1_KNOBS; k++) {
                    int id = fm1_page_knob(&n, k);
                    const fm1_pdef_t *d = fm1_page_def(&n, id);

                    if (id == PG_NONE) continue;
                    if (!d || !d->name) { bad++; continue; }
                    /* The knob column does not wrap and does not clip: a
                     * label one character too long overlaps its neighbour,
                     * and the screen reads "EQ2 FRQEQ2 GAINEQ2 RES". */
                    if (strlen(fm1_label_fit(d->name)) > FM1_LABEL_MAX) named++;
                }
            }
        }
    }
    check(bad == 0, "every knob id resolves to a real parameter", 0);
    check(named == 0, "every reachable label fits the knob column", 0);
}

/* Invariant 5: a menu button enters its menu at page 1, pressing it again
 * advances and wraps, and SHIFT returns to page 1 from anywhere. */
static void test_navigation(void)
{
    fm1_nav_t n;
    uint8_t i, np = FM1_MENU[M_EDIT].npages;
    int wrapped_ok, entered_at_one, shift_ok, other_menu_ok;

    fm1_nav_init(&n);
    fm1_nav_press(&n, M_EDIT, 0);
    entered_at_one = (n.menu == M_EDIT && n.page == 0);

    for (i = 0; i < np; i++) fm1_nav_press(&n, M_EDIT, 0);
    wrapped_ok = (n.page == 0);           /* npages presses returns to the top */

    fm1_nav_press(&n, M_EDIT, 0);
    fm1_nav_press(&n, M_EDIT, 0);
    fm1_nav_press(&n, M_EDIT, 1);         /* SHIFT from page 3 */
    shift_ok = (n.page == 0);

    /* Entering a DIFFERENT menu always lands on page 1, never on whatever
     * page that menu was left at -- "where was I" is not a question the
     * surface should ever ask. */
    fm1_nav_press(&n, M_EDIT, 0);
    fm1_nav_press(&n, M_FX, 0);
    other_menu_ok = (n.menu == M_FX && n.page == 0);

    check(entered_at_one, "a menu button enters at page 1", 0);
    check(wrapped_ok, "pressing it npages times wraps to page 1", 0);
    check(shift_ok, "SHIFT + the button returns to page 1", 0);
    check(other_menu_ok, "switching menus enters at page 1", 0);
}

/* Invariant 3: SELECT only ever chooses the Subject, and the selectors are
 * remembered per kind -- editing an EQ band must not disturb which voice you
 * were on, which is the whole reason the EQ page can select a band at all. */
static void test_select(void)
{
    fm1_nav_t n;
    int clamped_low, clamped_high, voice_survived, band_is_separate;

    fm1_nav_init(&n);
    fm1_nav_press(&n, M_EDIT, 0);         /* page 1, BRAIDS, subject VOICE */

    fm1_nav_select(&n, +2);
    fm1_nav_select(&n, +99);
    clamped_high = (n.sel[SUBJ_VOICE] == FM1_SUBJ_COUNT[SUBJ_VOICE] - 1);

    fm1_nav_select(&n, -99);
    clamped_low = (n.sel[SUBJ_VOICE] == 0);

    fm1_nav_select(&n, +2);               /* voice 3 */

    /* To the EQ page: SELECT must now move the band, leaving the voice be. */
    while (strcmp(fm1_nav_page(&n)->name, "EQ") != 0)
        fm1_nav_press(&n, M_EDIT, 0);

    fm1_nav_select(&n, +1);
    band_is_separate = (n.sel[SUBJ_BAND] == 1);
    voice_survived   = (n.sel[SUBJ_VOICE] == 2);

    check(clamped_high, "SELECT clamps at the last subject", 0);
    check(clamped_low, "SELECT clamps at the first subject", 0);
    check(band_is_separate, "SELECT moves the band on the EQ page", 0);
    check(voice_survived, "selecting a band leaves the voice alone", 0);
}

/* The stride is the only arithmetic in this header, and it is what lets one
 * page serve three EQ bands and four FX channels. Check it lands on the
 * parameters it claims to. */
static void test_stride(void)
{
    fm1_nav_t n;
    int band2_freq, fx3_p1, fx3_dest;

    fm1_nav_init(&n);
    fm1_nav_press(&n, M_EDIT, 0);
    while (strcmp(fm1_nav_page(&n)->name, "EQ") != 0)
        fm1_nav_press(&n, M_EDIT, 0);
    n.sel[SUBJ_BAND] = 1;                           /* MID */
    band2_freq = (fm1_page_knob(&n, 0) == P_EQ2_FRQ);

    fm1_nav_press(&n, M_FX, 0);                     /* EFFECT page */
    n.sel[SUBJ_FX] = 2;                             /* FX 3 */
    fx3_p1 = (fm1_page_knob(&n, 0) == G_FX3_P1);

    fm1_nav_press(&n, M_FX, 0);                     /* ROUTE page */
    fx3_dest = (fm1_page_knob(&n, 0) == G_FX3_DEST);

    check(band2_freq, "EQ band 2 selects EQ2 FRQ", 0);
    check(fx3_p1, "FX channel 3 selects FX3 P1", 0);
    check(fx3_dest, "the FX route page follows the same channel", 0);
}

/* The shape table's index IS the parameter value, so it has to agree with
 * P_SHAPE's range and with braids::MacroOscillatorShape. The count is
 * checkable here; the spelling is not checkable anywhere, which is why the
 * names were transcribed against the enum rather than from memory.
 *
 * The categories must also tile the range: contiguous, ascending, starting at
 * zero. A gap would make SHIFT + ALGORITHM skip shapes that are still
 * reachable one detent at a time -- the "where did it go" failure the
 * grouping exists to prevent. */
static void test_shapes(void)
{
    uint8_t c, s;
    int ascending = 1, covers = 1, named = 1;

    for (c = 0; c < FM1_NSHAPE_CATS; c++) {
        if (!FM1_SHAPE_CAT[c].name || !*FM1_SHAPE_CAT[c].name) named = 0;
        if (c && FM1_SHAPE_CAT[c].first <= FM1_SHAPE_CAT[c - 1].first)
            ascending = 0;
    }
    if (FM1_SHAPE_CAT[0].first != 0) covers = 0;
    if (FM1_SHAPE_CAT[FM1_NSHAPE_CATS - 1].first >= FM1_NSHAPES) covers = 0;

    for (s = 0; s < FM1_NSHAPES; s++) {
        uint8_t cat = fm1_shape_cat(s);
        if (cat >= FM1_NSHAPE_CATS) covers = 0;
        if (FM1_SHAPE_CAT[cat].first > s) covers = 0;
        if (cat + 1 < FM1_NSHAPE_CATS && FM1_SHAPE_CAT[cat + 1].first <= s)
            covers = 0;
        if (!FM1_SHAPE_NAME[s] || !*FM1_SHAPE_NAME[s]) named = 0;
    }

    check(FM1_NSHAPES == (int)FM1_PDEF[P_SHAPE].max + 1,
          "the shape table covers P_SHAPE's whole range", 0);
    check(named, "every shape and category has a name", 0);
    check(ascending, "category starts ascend", 0);
    check(covers, "every shape lands in exactly one category", 0);
}

/* The per-shape TIMBRE and COLOR labels.
 *
 * These replace the knob's own name on the EDIT page, so they are subject to
 * exactly the rule that produced "EQ2 FRQEQ2 GAINEQ2 RES" on a real screen:
 * the column does not wrap and does not clip, and one character too many
 * overlaps the neighbour. Ninety-six hand-written strings is ninety-six
 * chances to write a seventh character, so a test counts them.
 */
static void test_shape_knob_labels(void)
{
    uint8_t s;
    int fits = 1, present = 1, chord_implies_stepped = 1;

    for (s = 0; s < FM1_NSHAPES; s++) {
        const char *t = FM1_SHAPE_TIMBRE[s];
        const char *c = FM1_SHAPE_COLOR[s];

        if (!t || !*t || !c || !*c) { present = 0; continue; }
        if (strlen(t) > FM1_LABEL_MAX || strlen(c) > FM1_LABEL_MAX) fits = 0;

        /* Every chord shape quantises something -- the intervals on the
         * TRIPLE group, the chord table on WAVE PARAPHONIC. A chord badge
         * with no STEPPED badge would mean a chord that glides between
         * notes, which no shape here does. */
        if (fm1_shape_is_chord(s) && !fm1_shape_quantised(s))
            chord_implies_stepped = 0;
    }

    check(present, "every shape names both of its knobs", 0);
    check(fits, "every per-shape knob label fits the column", 0);
    check(chord_implies_stepped, "a chord shape always steps", 0);

    /* Spot checks against the renderers, so the tables cannot drift into
     * plausible-but-wrong. Each of these is a line of vendored source:
     * HARMONICS slides both knobs (RenderHarmonics), TRIPLE RING MOD offsets
     * two modulator pitches continuously (RenderTripleRingMod), and FM
     * quantises only its ratio (DigitalOscillator::Render). */
    check(!fm1_shape_quantised(24), "HARMONICS does not step", 0);
    check(!fm1_shape_quantised(13), "TRIPLE RING MOD does not step", 0);
    check(!fm1_shape_timbre_stepped(25) && fm1_shape_color_stepped(25),
          "FM steps its ratio and not its index", 0);
    check(fm1_shape_is_chord(40), "WAVE PARAPHONIC is a chord shape", 0);
}

/* A control that prints a UNIT has to mean it.
 *
 * DETUNE was declared K_CENTS, drawn as `v - 64`, and labelled cents, while
 * the engine gave it a +-50 cent span -- so the screen read +63 where the
 * oscillator moved 49, and one click was 0.78 of the unit printed next to it.
 * Nothing caught that because nothing related the declaration to the span.
 *
 * A bipolar control must at least be CENTRED: the distance to each end has to
 * match, or the knob cannot reach as far one way as the other and its centre
 * detent is not its zero. That is checkable from the table alone, and it is
 * the property the old row broke -- 0..127 around 64 can go 63 up and 64
 * down.
 */
static void test_bipolar_centred(void)
{
    int i, off_centre = 0, unreachable = 0;

    for (i = 0; i < NPARAM; i++) {
        const fm1_pdef_t *d = &FM1_PDEF[i];
        if (d->kind != K_CENTS && d->kind != K_OCT) continue;
        if ((uint32_t)d->def * 2u != (uint32_t)d->max) off_centre = 1;
        if (d->def == 0u || d->def >= d->max) unreachable = 1;
    }
    check(!off_centre, "a control in cents or octaves is centred on its def", 0);
    check(!unreachable, "and can be moved in both directions", 0);

    /* The two that prompted this, pinned by value so a later edit cannot
     * quietly reintroduce the mismatch. */
    check(FM1_PDEF[P_DETUNE].max == 100 && FM1_PDEF[P_DETUNE].def == 50,
          "DETUNE is +-50 cents, one per click", 0);
    check(FM1_PDEF[P_OCTAVE].max == 8 && FM1_PDEF[P_OCTAVE].def == 4,
          "OCTAVE is +-4 around centre", 0);
}

/* Encoder feel.
 *
 * This exists because the firmware shipped with every wide-range parameter
 * moving SIX steps per detent: the 48 oscillator shapes were walked five at
 * a time, and a value went 008, 014, 020 while the knob clicked once, once,
 * once. It was found by a human turning a knob in an emulator, which is a
 * slow way to find something a test can hold still.
 */
static void test_encoder_feel(void)
{
    fm1_accel_t a;
    uint32_t t = 10000;
    int32_t total;
    int i;

    /* One click, in isolation, is one step. Nothing else here matters as
     * much: this is the property that makes a control aimable. */
    memset(&a, 0, sizeof a);
    check(fm1_enc_accel(&a, t, 1, 127) == 1,
          "a single detent moves exactly one step", 0);
    check(fm1_enc_accel(&a, t + 500, 1, 127) == 1,
          "and so does the next one, after a pause", 0);

    /* A quick two-tap is two, not four: nudging a value by two is a thing
     * people do, and it must not overshoot. */
    memset(&a, 0, sizeof a);
    total  = fm1_enc_accel(&a, t, 1, 127);
    total += fm1_enc_accel(&a, t + 60, 1, 127);
    check(total == 2, "a quick two-tap moves exactly two", 0);

    /* Keep turning and it travels. Twenty detents at 50 ms -- one second of
     * deliberate turning -- must cover most of a 0..127 range rather than a
     * sixth of it.
     *
     * The bar was ">= 127", the whole range, until the curve was turned by a
     * human on hardware and came back "a LITTLE too fast". That number was a
     * design target picked on paper; this one is a design target corrected by
     * the panel, which outranks it. Relaxed deliberately, and the floor is
     * still high enough to fail the thing this guards -- a knob that needs
     * six turns to cross its range. */
    memset(&a, 0, sizeof a);
    total = 0;
    for (i = 0; i < 20; i++)
        total += fm1_enc_accel(&a, t + (uint32_t)i * 50u, 1, 127);
    printf("    twenty detents at 50 ms: %d steps\n", total);
    check(total >= 90, "a sustained turn covers most of the range", 0);

    /* The property the user actually asked for, and the one the previous
     * curve did NOT have: turn faster and it moves faster, at the same point
     * in the gesture. The old version multiplied by a count of consecutive
     * fast detents, so past its threshold every speed travelled identically
     * and the only way to go further was to go on longer.
     *
     * Measured over the same number of detents, so what is being compared is
     * steps-per-detent and nothing else. */
    {
        static const uint32_t ms[4] = { 110u, 80u, 50u, 20u };
        int32_t reach[4];
        int monotonic = 1, slow_is_gentle, fast_is_quick;
        int j;

        for (j = 0; j < 4; j++) {
            memset(&a, 0, sizeof a);
            reach[j] = 0;
            for (i = 0; i < 12; i++)
                reach[j] += fm1_enc_accel(&a, t + (uint32_t)i * ms[j], 1, 127);
            printf("    twelve detents at %3u ms: %3d steps\n",
                   ms[j], reach[j]);
        }
        for (j = 1; j < 4; j++)
            if (reach[j] <= reach[j - 1]) monotonic = 0;

        /* Not merely monotonic -- the span has to be worth having. A gentle
         * turn stays aimable and a fast one crosses the range. */
        slow_is_gentle = reach[0] <= 24;        /* 110 ms: about 2 a click */
        fast_is_quick  = reach[3] >= 80;        /* 20 ms: most of 0..127   */

        check(monotonic, "every step up in speed moves further per detent", 0);
        check(slow_is_gentle, "a gentle turn stays aimable", 0);
        check(fast_is_quick, "a fast spin covers most of the range in a dozen", 0);
    }

    /* Speed is read from the CURRENT turn, not from how long one has been
     * going: slowing down has to slow the value down again within a few
     * clicks, or the knob keeps running away after you have stopped pushing.
     * Twelve fast detents, then twelve slow ones. */
    {
        int32_t tail = 0;
        uint32_t when = t;

        memset(&a, 0, sizeof a);
        for (i = 0; i < 12; i++) { fm1_enc_accel(&a, when, 1, 127); when += 20u; }
        for (i = 0; i < 12; i++) {
            when += 110u;
            tail += fm1_enc_accel(&a, when, 1, 127);
        }
        printf("    twelve slow detents after a fast spin: %d steps\n", tail);
        check(tail <= 30, "slowing down slows the value down", 0);
    }

    /* Enums never accelerate. Six filter modes spun quickly must still land
     * where the clicks say, or the mode list becomes a slot machine. */
    memset(&a, 0, sizeof a);
    total = 0;
    for (i = 0; i < 6; i++)
        total += fm1_enc_accel(&a, t + (uint32_t)i * 10u, 1, 5);
    check(total == 6, "a short enum never accelerates", 0);

    /* Direction survives, and so does a multi-detent report: the panel can
     * deliver two or three detents in one poll when the loop is busy. */
    memset(&a, 0, sizeof a);
    check(fm1_enc_accel(&a, t, -1, 127) == -1,
          "turning the other way moves one step the other way", 0);
    memset(&a, 0, sizeof a);
    check(fm1_enc_accel(&a, t, 3, 127) == 3,
          "three detents in one poll are three steps", 0);

    /* Spinning FASTER THAN THE UI POLLS must not read as slower.
     *
     * This is the bug that reached the panel. fm1_enc_take() hands over
     * however many detents piled up since the last poll, so above the poll
     * rate the extra clicks arrive inside ONE call and the gap between calls
     * stops shrinking. Measuring that gap instead of the per-detent interval
     * pinned the multiplier to the poll rate however hard the knob was
     * turned -- and because changing a value forces a redraw, turning fast
     * LENGTHENED the poll, eventually past the threshold, at which point a
     * ten-detent burst was treated as a dead stop and paid one step each.
     *
     * Same wall-clock, same knob speed, reported at three different poll
     * rates. What the user did is identical in all three; only the firmware's
     * polling differs, so the value must travel about the same distance.
     */
    {
        int32_t got[3];
        int steady = 1, none_collapsed = 1;
        int j;
        static const struct { uint32_t per_poll, polls, ms; } rate[3] = {
            { 1u, 24u,  25u },   /* loop keeps up: one detent per poll   */
            { 3u,  8u,  75u },   /* redraw costs: three pile up per poll */
            { 8u,  3u, 200u },   /* slow redraw: a burst per poll, over
                                  * the old "stopped" threshold          */
        };

        for (j = 0; j < 3; j++) {
            memset(&a, 0, sizeof a);
            got[j] = 0;
            for (i = 0; i < (int)rate[j].polls; i++)
                got[j] += fm1_enc_accel(&a,
                                        t + (uint32_t)i * rate[j].ms,
                                        (int32_t)rate[j].per_poll, 127);
            printf("    24 detents, %u per poll every %3u ms: %3d steps\n",
                   rate[j].per_poll, rate[j].ms, got[j]);
        }

        /* All three are the same gesture -- 24 detents in 600 ms -- so they
         * must land within a third of each other, and none may collapse to
         * one-step-per-detent. */
        for (j = 1; j < 3; j++) {
            int32_t lo = got[0] < got[j] ? got[0] : got[j];
            int32_t hi = got[0] < got[j] ? got[j] : got[0];
            if (hi > lo + lo / 2) steady = 0;
            if (got[j] <= 24) none_collapsed = 0;
        }
        check(none_collapsed,
              "a burst bigger than the poll never reads as a dead stop", 0);
        check(steady,
              "one knob speed travels the same, whatever the poll rate", 0);
    }
}

int main(void)
{
    printf("menu and page map\n");
    test_reachable();
    test_bipolar_centred();
    test_encoder_feel();
    test_shapes();
    test_shape_knob_labels();
    test_no_duplicate_on_page();
    test_ids_in_range();
    test_navigation();
    test_select();
    test_stride();

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
