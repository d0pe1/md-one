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
        d.SetParam(v, P_F1_TYPE, F_OFF);    /* the bus is under test, not the filters */
        d.SetParam(v, P_F2_TYPE, F_OFF);
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

/* --- the FX patchbay ------------------------------------------------------
 *
 * The routing is what is worth pinning. Every effect is a pure function of
 * its input; the part that can be subtly wrong is where signal GOES -- and
 * especially that a backward cable costs exactly one sample rather than a
 * block, which is the whole reason the patchbay is evaluated per sample.
 */
static void rack_run(fm1::fx_rack_t *r, const int32_t *src,
                     int32_t *out, uint32_t frames, uint8_t voice_dest)
{
    int32_t in_l[FM1_FX_CHANS], in_r[FM1_FX_CHANS];
    for (uint32_t n = 0; n < FM1_FX_CHANS; n++) in_l[n] = in_r[n] = 0;

    for (uint32_t i = 0; i < frames; i++) {
        out[2 * i] = out[2 * i + 1] = 0;

        /* One "voice" feeding the patchbay at voice_dest. */
        if (voice_dest == DEST_OUT) {
            out[2 * i]     += src[2 * i];
            out[2 * i + 1] += src[2 * i + 1];
        } else if (voice_dest != DEST_OFF) {
            in_l[voice_dest - DEST_FX1] += src[2 * i];
            in_r[voice_dest - DEST_FX1] += src[2 * i + 1];
        }

        uint8_t act[FM1_FX_CHANS];
        uint32_t nact = fx_rack_active(r, act);
        fx_rack_sample(r, r->cursor, act, nact, in_l, in_r,
                       &out[2 * i], &out[2 * i + 1]);
        r->cursor = (r->cursor + 1u) & FM1_FX_DMASK;
    }
}

static void test_rack(void)
{
    printf("\nfx patchbay\n");

    static fm1::fx_rack_t r;
    static int32_t src[FM1_BLOCK * 2], out[FM1_BLOCK * 2];

    for (uint32_t i = 0; i < FM1_BLOCK; i++) {
        src[2 * i]     =  (int32_t)(i % 64) * 200 - 6000;
        src[2 * i + 1] = -(int32_t)(i % 64) * 200 + 6000;
    }

    /* A voice straight to OUT passes through untouched. */
    fx_rack_init(&r);
    rack_run(&r, src, out, FM1_BLOCK, DEST_OUT);
    int direct = 1;
    for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++)
        if (out[i] != src[i]) { direct = 0; break; }
    ok(direct, "voice -> OUT is bit-exact", "the dry path is not clean");

    /* The headline rule: a channel whose cable is unplugged produces nothing,
     * and nothing routed into it is heard. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_WIDTH);
    fx_chan_set_dest(&r.ch[0], DEST_OFF);
    rack_run(&r, src, out, FM1_BLOCK, DEST_FX1);
    int silent = 1;
    for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++)
        if (out[i]) { silent = 0; break; }
    ok(silent, "FX dest OFF silences everything feeding it",
       "audio escaped an unplugged channel");

    /* ...and plugging the same channel in makes it audible again, so the test
     * above is not passing because the signal was broken to begin with. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_WIDTH);
    fx_chan_set_dest(&r.ch[0], DEST_OUT);
    rack_run(&r, src, out, FM1_BLOCK, DEST_FX1);
    int audible = 0;
    for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++)
        if (out[i]) { audible = 1; break; }
    ok(audible, "plugging it back in restores the signal", "still silent");

    /* Two sources into one channel sum at its input -- the "no input control"
     * claim. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_NONE);        /* a wire */
    fx_chan_set_dest(&r.ch[0], DEST_OUT);
    {
        int32_t in_l[FM1_FX_CHANS], in_r[FM1_FX_CHANS];
        for (uint32_t n = 0; n < FM1_FX_CHANS; n++) in_l[n] = in_r[n] = 0;
        int summed = 1;
        for (uint32_t i = 0; i < 64; i++) {
            int32_t ol = 0, orr = 0;
            in_l[0] += src[2 * i];  in_r[0] += src[2 * i + 1];
            in_l[0] += src[2 * i];  in_r[0] += src[2 * i + 1];
            uint8_t act[FM1_FX_CHANS];
            uint32_t nact = fx_rack_active(&r, act);
            fx_rack_sample(&r, r.cursor, act, nact, in_l, in_r, &ol, &orr);
            r.cursor = (r.cursor + 1u) & FM1_FX_DMASK;
            if (ol != 2 * src[2 * i]) { summed = 0; break; }
        }
        ok(summed, "two sources into one channel sum at its input",
           "inputs are not mixing");
    }

    /* FORWARD cable: FX1 -> FX2 -> OUT arrives with NO delay. Both channels
     * are wires, so the output must equal the input sample for sample. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_NONE);
    fx_chan_set_dest(&r.ch[0], DEST_FX2);
    fx_chan_set_type(&r.ch[1], FX_NONE);
    fx_chan_set_dest(&r.ch[1], DEST_OUT);
    rack_run(&r, src, out, FM1_BLOCK, DEST_FX1);
    int fwd = 1;
    for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++)
        if (out[i] != src[i]) { fwd = 0; break; }
    ok(fwd, "forward cable FX1->FX2->OUT costs no delay",
       "a forward cable is delaying");

    /* BACKWARD cable: FX2 -> FX1 must cost EXACTLY one sample. FX1 is a wire
     * to OUT, FX2 is a wire back to FX1, and the voice feeds FX2. So the
     * signal goes FX2 (this sample) -> FX1 (next sample) -> OUT. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_NONE);
    fx_chan_set_dest(&r.ch[0], DEST_OUT);
    fx_chan_set_type(&r.ch[1], FX_NONE);
    fx_chan_set_dest(&r.ch[1], DEST_FX1);
    rack_run(&r, src, out, FM1_BLOCK, DEST_FX2);

    int delayed = 1;
    for (uint32_t i = 1; i < FM1_BLOCK; i++)
        if (out[2 * i] != src[2 * (i - 1)]) { delayed = 0; break; }
    ok(delayed && out[0] == 0,
       "backward cable FX2->FX1 costs exactly one sample",
       "a backward cable is not one sample -- check evaluation order");

    /* A self cable is the same mechanism: FX1 -> FX1 is one sample of
     * feedback, which is a one-pole, not a 5.8 ms echo. It must stay finite. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_NONE);
    fx_chan_set_dest(&r.ch[0], DEST_FX1);
    rack_run(&r, src, out, FM1_BLOCK, DEST_FX1);
    ok(1, "self cable FX1->FX1 does not hang", "");

    /* Runaway feedback must saturate, never wrap. A wire looped to itself has
     * loop gain 1, so it integrates; the clamp is what keeps that an
     * ever-louder scream instead of white noise from int32 wrapping. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_NONE);
    fx_chan_set_dest(&r.ch[0], DEST_FX1);       /* into itself */
    {
        int32_t in_l[FM1_FX_CHANS], in_r[FM1_FX_CHANS];
        for (uint32_t n = 0; n < FM1_FX_CHANS; n++) in_l[n] = in_r[n] = 0;
        int bounded = 1;
        for (uint32_t i = 0; i < 20000; i++) {
            int32_t ol = 0, orr = 0;
            in_l[0] += 20000; in_r[0] += 20000;     /* drive it hard, forever */
            uint8_t act[FM1_FX_CHANS];
            uint32_t nact = fx_rack_active(&r, act);
            fx_rack_sample(&r, r.cursor, act, nact, in_l, in_r, &ol, &orr);
            r.cursor = (r.cursor + 1u) & FM1_FX_DMASK;
            if (r.ch[0].acc[0] > FM1_FX_CEIL || r.ch[0].acc[0] < -FM1_FX_CEIL) {
                bounded = 0; break;
            }
        }
        ok(bounded, "runaway feedback clamps instead of wrapping",
           "the accumulator escaped its ceiling -- int32 would wrap to noise");
    }

    /* WIDTH at centre is unity, and at 0 is mono. Cheap, and catches a lost
     * bit in the mid/side maths -- an earlier `* 2` made centre 2x wide. */
    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_WIDTH);
    fx_chan_set_p1(&r.ch[0], 64);
    fx_chan_set_dest(&r.ch[0], DEST_OUT);
    rack_run(&r, src, out, FM1_BLOCK, DEST_FX1);
    int unity = 1;
    for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++)
        if (out[i] - src[i] > 1 || src[i] - out[i] > 1) { unity = 0; break; }
    ok(unity, "WIDTH at centre is unity (within 1 LSB)", "mid/side is not transparent");

    fx_rack_init(&r);
    fx_chan_set_type(&r.ch[0], FX_WIDTH);
    fx_chan_set_p1(&r.ch[0], 0);
    fx_chan_set_dest(&r.ch[0], DEST_OUT);
    rack_run(&r, src, out, FM1_BLOCK, DEST_FX1);
    int mono = 1;
    for (uint32_t i = 0; i < FM1_BLOCK; i++)
        if (out[2 * i] != out[2 * i + 1]) { mono = 0; break; }
    ok(mono, "WIDTH at 0 collapses to mono", "channels still differ");

    /* The folder must stay in range for any input the bus can carry, and must
     * never return the clipped value -- folding that saturates is clipping
     * with extra steps. */
    int folded_ok = 1;
    for (int32_t x = -4 * 32768; x <= 4 * 32768; x += 97) {
        int32_t y = fm1::fx_fold1(x, Q12_ONE * 3, 0);
        if (y > Q15_ONE || y < -Q15_ONE) { folded_ok = 0; break; }
    }
    ok(folded_ok, "FOLD stays in range across +-4.0 at 3x drive", "folder escaped Q15");
}

/* --- the mixer meters ----------------------------------------------------- */

static void test_meters(void)
{
    printf("\nmixer meters\n");

    static fm1::Drone d;
    static int16_t buf[FM1_BLOCK * 2];

    d.Init();
    d.SetGlobal(G_SPREAD, 0);
    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        d.SetGate(v, v < 2);                    /* voices 1 and 2 only */
        d.SetParam(v, P_FM, 64);
        d.SetParam(v, P_MOD, 64);
        d.SetParam(v, P_LEVEL, v == 0 ? 127 : 40);
        if (v < 2) d.NoteOn(v, 45);
    }
    for (int b = 0; b < 16; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
    }

    uint8_t m0 = d.Meter(0), m1 = d.Meter(1), m2 = d.Meter(2);
    printf("    meters: %3d %3d %3d %3d\n", m0, m1, m2, d.Meter(3));

    ok(m0 > 0, "a sounding voice reads on its meter", "meter stayed at zero");
    ok(m2 == 0, "a silent voice reads zero", "a gated-off voice is metering");

    /* The meter must follow the fader, or it is describing something other
     * than what the channel contributes. */
    ok(m0 > m1, "a louder voice reads higher than a quieter one",
       "the meter does not track LEVEL");

    /* And it must FALL. A peak meter that latches looks plausible for a few
     * seconds and then lies forever. */
    for (uint8_t v = 0; v < FM1_VOICES; v++) d.SetGate(v, 0);
    for (int b = 0; b < 4; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
    }
    uint8_t after = d.Meter(0);
    ok(after < m0, "the meter falls once the voice stops", "the meter latched");

    for (int b = 0; b < 200; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
    }
    ok(d.Meter(0) == 0, "and reaches zero", "the meter never settles");
}

int main(void)
{
    test_soft_clip();
    test_bus();
    test_rack();
    test_meters();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
