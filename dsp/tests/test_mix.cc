/* SPDX-License-Identifier: MIT
 *
 * The mix bus and the limiter.
 *
 * These are the invariants from docs/SIGNAL_PATH.md section 4. They exist
 * because the previous mix stage saturated on every voice add, so four loud
 * voices distorted against each other rather than the sum being limited once
 * -- and because a single voice could only reach about 0.24 of full scale,
 * throwing away most of the output range.
 *
 * Neither defect was caught by test_osc or test_comb, because neither renders
 * through Drone::Render. Written to FAIL if either returns.
 */
#include <stdio.h>

#include "../shared/fm1_fixed.h"
#include "../engine/fm1_drone.h"

static int failures = 0;

static void ok(int cond, const char *what, const char *detail)
{
    printf("  %-54s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) {
        printf("  %-54s    %s\n", "", detail);
        failures++;
    }
}

/* --- the limiter ---------------------------------------------------------- */

static void test_soft_clip(void)
{
    printf("soft clip\n");

    /* Below the knee it must be bit-exact identity: quiet material is not a
     * place to be creative. */
    int identity = 1;
    for (int32_t x = -FM1_CLIP_KNEE; x <= FM1_CLIP_KNEE; x += 7)
        if (fm1_soft_clip(x) != (q15_t)x) { identity = 0; break; }
    ok(identity, "identity below the knee", "a quiet sample was altered");

    /* Never exceeds Q15 at any input the bus can produce. Four voices at full
     * level is about 4.0; 8.0 is double that and still must not wrap. */
    int bounded = 1;
    for (int32_t x = -8 * 32768; x <= 8 * 32768; x += 13) {
        q15_t y = fm1_soft_clip(x);
        if (y > Q15_ONE || y < Q15_MIN) { bounded = 0; break; }
    }
    ok(bounded, "bounded to Q15 across +-8.0 of bus range", "wrapped");

    /* Monotonic. A non-monotonic limiter folds the waveform instead of
     * compressing it, which is a fuzz box, not a safety net. */
    int monotonic = 1;
    q15_t prev = fm1_soft_clip(-8 * 32768);
    for (int32_t x = -8 * 32768 + 3; x <= 8 * 32768; x += 3) {
        q15_t y = fm1_soft_clip(x);
        if (y < prev) { monotonic = 0; break; }
        prev = y;
    }
    ok(monotonic, "monotonic (compresses, never folds)", "output went backwards");

    /* Continuous at the knee: a step there is an audible click on any signal
     * crossing it, which on a sustained drone is most of them. */
    int32_t below = fm1_soft_clip(FM1_CLIP_KNEE);
    int32_t above = fm1_soft_clip(FM1_CLIP_KNEE + 1);
    ok(above - below <= 1, "continuous across the knee", "stepped at 0.5");

    /* Odd symmetry: asymmetric clipping adds even harmonics and DC offset. */
    int symmetric = 1;
    for (int32_t x = 1; x < 4 * 32768; x += 11)
        if (fm1_soft_clip(-x) != -fm1_soft_clip(x)) { symmetric = 0; break; }
    ok(symmetric, "odd symmetric (no DC, no even harmonics)", "asymmetric");

    /* Reaches full scale rather than stopping short -- a limiter that tops out
     * at 0.8 is just quiet. */
    ok(fm1_soft_clip(FM1_CLIP_HARD) == Q15_ONE,
       "reaches full scale at 1.5 and holds", "never arrives at 1.0");
}

/* --- the bus -------------------------------------------------------------- */

static int32_t peak_of(const int16_t *buf, uint32_t frames)
{
    int32_t pk = 0;
    for (uint32_t i = 0; i < frames * 2u; i++) {
        int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
        if (a > pk) pk = a;
    }
    return pk;
}

/* Render `voices` voices at the given level and pan, and report the peak.
 * Same note, no detune, no spread, no drift: the voices are bit-identical, so
 * four of them are exactly four times one and the arithmetic is checkable. */
static int32_t render_peak(uint8_t voices, uint8_t level, uint8_t pan)
{
    static fm1::Drone d;
    static int16_t buf[FM1_BLOCK * 2];

    d.Init();
    d.SetGlobal(G_SPREAD, 0);               /* or the voices detune apart */
    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        d.SetGate(v, v < voices);
        if (v >= voices) continue;
        d.SetParam(v, P_LEVEL, level);
        d.SetParam(v, P_PAN, pan);
        d.SetParam(v, P_DETUNE, 64);        /* centre: no offset */
        d.SetParam(v, P_COMB_MIX, 0);       /* the bus is under test, not the comb */
        d.SetParam(v, P_FM, 64);            /* drift off: deterministic */
        d.SetParam(v, P_MOD, 64);
        d.NoteOn(v, 45);
    }

    /* Discard the first blocks: the oscillators need a few to settle. */
    int32_t pk = 0;
    for (int b = 0; b < 12; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
        if (b >= 8) {
            int32_t p = peak_of(buf, FM1_BLOCK);
            if (p > pk) pk = p;
        }
    }
    return pk;
}

static void test_bus(void)
{
    printf("\nmix bus\n");

    int32_t one  = render_peak(1, 127, 64);     /* centre */
    int32_t hard = render_peak(1, 127, 127);    /* hard right */
    int32_t four = render_peak(4, 127, 64);

    printf("    one voice, centre:  %6d / %d\n", one,  Q15_ONE);
    printf("    one voice, panned:  %6d / %d\n", hard, Q15_ONE);
    printf("    four voices:        %6d / %d\n", four, Q15_ONE);

    /* A single voice must not reach the limiter at all. This is the gain
     * staging invariant that matters: if one voice already needs limiting,
     * there is no headroom left for the other three and every mix distorts.
     *
     * Note what is deliberately NOT asserted -- an absolute peak. The peak
     * depends on the oscillator's own level (this shape reaches about 0.68 of
     * full scale), not on the bus, so testing it would be testing Braids. */
    ok(one < FM1_CLIP_KNEE, "one voice stays below the limiter knee",
       "a single voice is already being compressed");

    /* Linear pan law: hard-panned is twice centre in that channel. Catches a
     * pan law that silently became constant-power, or an off-by-one in the
     * 0..127 mapping. */
    ok(hard > one * 18 / 10 && hard < one * 22 / 10,
       "hard pan is 2x centre pan (linear law)", "the pan law is not linear");

    /* Four bit-identical voices are exactly 4x one on the bus, so the output
     * must be what the limiter curve predicts from one voice. This is the
     * invariant that per-voice saturation broke: the old code clipped each
     * add, so four voices landed far below this and sounded like splatter.
     *
     * No magic constant -- the expectation is computed from the measurement. */
    int32_t predicted = fm1_soft_clip(4 * one);
    int32_t err = four > predicted ? four - predicted : predicted - four;
    printf("    predicted from one: %6d  (error %d)\n", predicted, err);

    ok(err < predicted / 32, "four voices land where the limiter predicts",
       "the bus is not summing cleanly, or saturates before the limiter");

    ok(four <= Q15_ONE, "four correlated voices stay inside Q15",
       "wrapped or overflowed");

    /* Level 0 is silence. Trivial, and exactly what breaks when gain moves to
     * a new fixed-point format. */
    ok(render_peak(1, 0, 64) == 0, "level 0 is silent", "a muted voice still sounds");
}

int main(void)
{
    test_soft_clip();
    test_bus();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
