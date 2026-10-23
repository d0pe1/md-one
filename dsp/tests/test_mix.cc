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
#include <math.h>

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
        d.SetParam(v, P_DETUNE, 50);        /* centre: no offset */
        d.SetParam(v, P_F1_TYPE, F_OFF);    /* the bus is under test, not the filters */
        d.SetParam(v, P_F2_TYPE, F_OFF);
        d.SetParam(v, P_FM, 64);            /* drift off: deterministic */
        d.SetParam(v, P_MOD, 64);
        d.NoteOn(v, 45);
    }

    /* Discard the first blocks: the oscillators need a few to settle. */
    int32_t pk = 0;
    /* A test rig sets a patch, it does not turn a knob: without this the
     * slewed parameters are still on their way when the render below
     * samples them, and every level- or pan-dependent assertion reads a
     * value nobody set. The firmware does the same after a patch load. */
    d.SnapParams();
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

    /* CONSTANT-POWER pan law: centre is 0.707 of hard over in that channel,
     * not 0.5. This assertion used to demand 0.5 and was the thing defending
     * a pan control that doubled as a 6 dB volume control -- the centre, where
     * a drone bed sits, being the quietest place on the knob.
     *
     * The bound is wide on purpose and the ratio will read a little under the
     * theoretical 1.414: a hard-panned voice at full level is up against the
     * soft clipper, so its peak is held back while the centred one is not.
     * What the test has to separate is 1.41 from 2.0 -- a law that silently
     * goes back to linear -- and an off-by-one in the 0..127 mapping, which
     * shows up at the ends rather than here. */
    ok(hard * 100 > one * 125 && hard * 100 < one * 170,
       "centre pan is 0.707 of hard over (constant power)",
       "the pan law is not constant-power");

    /* The ends are exact by construction (fm1_pan_table.h): unity into the
     * near channel, silence into the far one. A voice panned hard must not
     * lose level to the law, and must not leak across. */
    ok(render_peak(1, 127, 0) > hard * 9 / 10,
       "hard left matches hard right in level",
       "the pan table is not symmetric at the ends");

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
    /* A test rig sets a patch, it does not turn a knob: without this the
     * slewed parameters are still on their way when the render below
     * samples them, and every level- or pan-dependent assertion reads a
     * value nobody set. The firmware does the same after a patch load. */
    d.SnapParams();
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

    /* The ladder must be ordered and reach the top, or segments are dead. */
    int ladder = (FM1_METER_STEPS[FM1_METER_SEGS - 1] == 127);
    for (int i = 1; i < FM1_METER_SEGS; i++)
        if (FM1_METER_STEPS[i] <= FM1_METER_STEPS[i - 1]) ladder = 0;
    ok(ladder, "the meter ladder ascends and reaches full scale",
       "a segment is unreachable or out of order");

    for (int b = 0; b < 200; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
    }
    ok(d.Meter(0) == 0, "and reaches zero", "the meter never settles");
    ok(!d.Clipping(0), "the clip flag releases", "the channel is red forever");

    /* Drive a voice into full scale through a resonant comb and check the red
     * actually arrives -- a clip indicator that never fires is decoration. */
    d.Init();
    d.SetGlobal(G_SPREAD, 0);
    d.SetParam(0, P_LEVEL, 127);
    /* A resonant bandpass tuned to the note, not a comb: the comb normalises
     * its own resonant gain (fm1_comb_set_feedback), so it cannot drive a
     * channel past full scale by design. The SVF can. */
    d.SetParam(0, P_F1_TYPE, F_BP);
    d.SetParam(0, P_F1_CUT, 45);        /* the note being played */
    d.SetParam(0, P_F1_RES, 127);
    d.SetParam(0, P_F1_MIX, 127);
    d.SetParam(0, P_F2_TYPE, F_BP);     /* a second one, in series */
    d.SetParam(0, P_F2_CUT, 45);
    d.SetParam(0, P_F2_RES, 127);
    d.SetParam(0, P_F2_MIX, 127);
    d.SetParam(0, P_FM, 64);
    d.SetParam(0, P_MOD, 64);
    d.NoteOn(0, 45);
    int sawRed = 0; uint8_t hottest = 0;
    /* A test rig sets a patch, it does not turn a knob: without this the
     * slewed parameters are still on their way when the render below
     * samples them, and every level- or pan-dependent assertion reads a
     * value nobody set. The firmware does the same after a patch load. */
    d.SnapParams();
    for (int b = 0; b < 400 && !sawRed; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
        if (d.Meter(0) > hottest) hottest = d.Meter(0);
        if (d.Clipping(0)) sawRed = 1;
    }
    printf("    hottest with resonant BP: %d / 127\n", hottest);
    ok(sawRed, "a channel driven to full scale reports clipping",
       "the clip indicator never fires");
}

/* --- the channel EQ and drive --------------------------------------------- */

/* Energy at a given note, by running the band through its own SVF and summing
 * the band-pass output. Crude, but it answers the only question these tests
 * ask: did this band get louder or quieter? */
static int32_t band_energy(const int16_t *buf, uint32_t n, uint8_t note)
{
    /* Trapezoidal, like everything else now. This analyser was the last
     * Chamberlin left anywhere, which is a nice illustration of why the
     * sweep was worth doing: a measurement tool with the fs/6 ceiling would
     * have reported a high band as quieter than it is. */
    fm1_tpt_t s;
    fm1_tpt_init(&s);
    fm1_tpt_set_pitch(&s, (int32_t)note << 7);
    fm1_tpt_set_res(&s, 90);

    int64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        fm1_tpt_out_t o = fm1_tpt_tick(&s, buf[2 * i]);
        sum += (int64_t)o.bp * o.bp;
    }
    return (int32_t)(sum >> 20);
}

/* Render one voice with an EQ setting and return the block. */
static void render_eq(int16_t *buf, uint8_t band, uint8_t mode,
                      uint8_t freq, uint8_t gain, uint8_t drive)
{
    static fm1::Drone d;
    d.Init();
    d.SetGlobal(G_SPREAD, 0);
    for (uint8_t v = 1; v < FM1_VOICES; v++) d.SetGate(v, 0);
    d.SetParam(0, P_FM, 64);
    d.SetParam(0, P_MOD, 64);
    d.SetParam(0, P_LEVEL, 100);
    d.SetParam(0, P_DRIVE, drive);
    d.SetParam(0, P_EQ(band, P_EQ_MODE), mode);
    d.SetParam(0, P_EQ(band, P_EQ_FRQ),  freq);
    d.SetParam(0, P_EQ(band, P_EQ_GAIN), gain);
    d.NoteOn(0, 45);

    /* A test rig sets a patch, it does not turn a knob: without this the
     * slewed parameters are still on their way when the render below
     * samples them, and every level- or pan-dependent assertion reads a
     * value nobody set. The firmware does the same after a patch load. */
    d.SnapParams();
    for (int b = 0; b < 10; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
    }
}

static void test_eq(void)
{
    printf("\nchannel EQ\n");

    static int16_t flat[FM1_BLOCK * 2], cut[FM1_BLOCK * 2], boost[FM1_BLOCK * 2];

    /* Flat must be BIT-EXACTLY flat. An EQ at centre that colours the signal
     * is worse than no EQ, because every patch inherits it invisibly.
     *
     * Tested on the block directly rather than through Drone, because two
     * renders of the same patch are NOT bit-identical: Braids drives its
     * jitter from a process-global RNG that Init() does not reset. Nothing
     * here can A/B two renders sample by sample. */
    {
        fm1_eq_t e;
        fm1_eq_init(&e);
        int flatbits = 1;
        for (int32_t x = -Q15_ONE; x <= Q15_ONE; x += 37)
            if (fm1_eq_tick(&e, (q15_t)x) != (q15_t)x) { flatbits = 0; break; }
        ok(flatbits, "an EQ with every band at centre is a bit-exact no-op",
           "a flat EQ is colouring the signal");
    }

    render_eq(flat, EQ_MID, EQB_PEAK, 72, 64, 0);

    /* A mid boost must raise energy at the band, a cut must lower it. The
     * expectation is relative to the same render with the gain centred, so
     * nothing depends on the oscillator's absolute level. */
    int32_t e_flat = band_energy(flat, FM1_BLOCK, 72);
    render_eq(boost, EQ_MID, EQB_PEAK, 72, 120, 0);
    render_eq(cut,   EQ_MID, EQB_PEAK, 72,   0, 0);
    int32_t e_boost = band_energy(boost, FM1_BLOCK, 72);
    int32_t e_cut   = band_energy(cut,   FM1_BLOCK, 72);
    printf("    mid band energy: cut %d, flat %d, boost %d\n", e_cut, e_flat, e_boost);

    ok(e_boost > e_flat, "a mid boost adds energy at the band", "boost did nothing");
    ok(e_cut   < e_flat, "a mid cut removes it", "cut did nothing");

    /* The low band's pass mode is a HIGH-pass: it must remove the bottom. */
    render_eq(cut, EQ_LOW, EQB_PASS, 80, 64, 0);
    int32_t lo_flat = band_energy(flat, FM1_BLOCK, 30);
    int32_t lo_hp   = band_energy(cut,  FM1_BLOCK, 30);
    printf("    low energy: flat %d, highpassed %d\n", lo_flat, lo_hp);
    ok(lo_hp < lo_flat, "LOW in pass mode is a high-pass",
       "the low band's pass mode kept the lows");

    /* ...and the high band's is a LOW-pass, removing the top. */
    render_eq(cut, EQ_HIGH, EQB_PASS, 50, 64, 0);
    int32_t hi_flat = band_energy(flat, FM1_BLOCK, 100);
    int32_t hi_lp   = band_energy(cut,  FM1_BLOCK, 100);
    printf("    high energy: flat %d, lowpassed %d\n", hi_flat, hi_lp);
    ok(hi_lp < hi_flat, "HIGH in pass mode is a low-pass",
       "the high band's pass mode kept the highs");
}

static void test_saturator(void)
{
    printf("\ndrive\n");

    ok(fm1_saturate(0) == 0, "silence in, silence out", "the saturator has an offset");

    int symmetric = 1, monotonic = 1, bounded = 1;
    int32_t prev = fm1_saturate(-Q15_ONE);
    for (int32_t x = -Q15_ONE; x <= Q15_ONE; x += 7) {
        int32_t y = fm1_saturate(x);
        if (y > Q15_ONE || y < -Q15_ONE) bounded = 0;
        if (y < prev) monotonic = 0;
        if (fm1_saturate(-x) != -y) symmetric = 0;
        prev = y;
    }
    ok(bounded,   "bounded to Q15", "the saturator overflowed");
    ok(monotonic, "monotonic", "the curve folds instead of compressing");
    ok(symmetric, "odd symmetric (odd harmonics only, no DC)", "asymmetric");

    /* Full scale in, full scale out: a drive stage that loses level is just a
     * quiet drive stage. */
    ok(fm1_saturate(Q15_ONE) == Q15_ONE, "unity at full scale", "the curve loses level");

    /* ...and it must compress in between, or it is a wire. */
    ok(fm1_saturate(Q15_ONE / 2) > Q15_ONE / 2,
       "gain below full scale (the curve bends)", "the saturator is linear");
}

/* --- the VCA envelope ----------------------------------------------------- */

static int32_t max_step(const int16_t *buf, uint32_t frames)
{
    int32_t worst = 0;
    for (uint32_t i = 1; i < frames; i++) {
        int32_t d = buf[2 * i] - buf[2 * (i - 1)];
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    return worst;
}

static void test_env(void)
{
    printf("\nVCA envelope\n");

    /* Walk the whole AHDSR and check each stage actually happens, in order.
     * A stage that is silently skipped is the classic envelope bug: it still
     * sounds like an envelope, just not the one on the panel. */
    {
        fm1_env_t e;
        fm1_env_init(&e);
        fm1_env_set_attack(&e, 10);
        fm1_env_set_hold(&e, 40);
        fm1_env_set_decay(&e, 20);
        fm1_env_set_sustain(&e, 64);
        fm1_env_set_release(&e, 10);

        int seen[ENV_STAGES] = { 0 };
        int32_t peak = 0;
        fm1_env_gate(&e, 1);
        for (int i = 0; i < 44100 * 4; i++) {
            seen[e.stage] = 1;
            int32_t l = fm1_env_tick(&e);
            if (l > peak) peak = l;
            if (e.stage == ENV_SUSTAIN && i > 1000) break;
        }
        printf("    stages seen: A%d H%d D%d S%d, peak %d%%\n",
               seen[ENV_ATTACK], seen[ENV_HOLD], seen[ENV_DECAY],
               e.stage == ENV_SUSTAIN, 100 * peak / FM1_ENV_ONE);

        ok(seen[ENV_ATTACK], "ATTACK runs", "the attack stage was skipped");
        ok(seen[ENV_HOLD],   "HOLD runs",   "the hold stage was skipped");
        ok(seen[ENV_DECAY],  "DECAY runs",  "the decay stage was skipped");
        ok(e.stage == ENV_SUSTAIN, "and it settles in SUSTAIN",
           "the envelope never reached sustain");
        ok(peak == FM1_ENV_ONE, "the peak reaches full scale",
           "the attack stalls short of unity");

        /* Sustain must hold at the knob, not at full. */
        int32_t want = (64 * FM1_ENV_ONE) / 127;
        int32_t got = fm1_env_tick(&e);
        ok(got == want, "SUSTAIN holds at its level, not at full",
           "the sustain level is being ignored");

        /* And release must take it to zero and then idle. */
        fm1_env_gate(&e, 0);
        for (int i = 0; i < 44100 * 4 && e.stage != ENV_IDLE; i++) fm1_env_tick(&e);
        ok(e.stage == ENV_IDLE && e.level == 0,
           "RELEASE reaches zero and the voice goes idle",
           "the envelope never fully closes");
        ok(fm1_env_idle(&e), "and reports idle so the voice can be skipped",
           "a finished voice still costs cycles");
    }

    /* HOLD must actually hold -- flat, at the top, for as long as asked.
     * Without it a slow swell has no peak, only a corner where the attack
     * and decay curves meet. */
    {
        fm1_env_t e;
        fm1_env_init(&e);
        fm1_env_set_attack(&e, 0);
        fm1_env_set_hold(&e, 60);
        fm1_env_set_decay(&e, 0);
        fm1_env_set_sustain(&e, 0);
        fm1_env_gate(&e, 1);

        int held = 0, flat = 1;
        for (int i = 0; i < 44100 * 4; i++) {
            fm1_env_tick(&e);
            if (e.stage == ENV_HOLD) {
                held++;
                if (e.level != FM1_ENV_ONE) flat = 0;
            } else if (held) break;
        }
        printf("    hold 60 held for %d samples (%.0f ms), flat %d\n",
               held, held * 1000.0 / 44100.0, flat);
        ok(held > 100, "HOLD holds for a measurable time", "hold did nothing");
        ok(flat, "and holds FLAT at full scale", "the hold stage is drifting");
    }

    /* A slow setting must actually be slower. Catches a rate table that is
     * backwards or flat -- which listening would not reveal for twenty
     * seconds, by which time you are debugging the wrong thing. */
    {
        fm1_env_t fast, slow;
        fm1_env_init(&fast); fm1_env_init(&slow);
        fm1_env_set_attack(&fast, 20);
        fm1_env_set_attack(&slow, 100);
        fm1_env_gate(&fast, 1); fm1_env_gate(&slow, 1);
        for (int i = 0; i < 44100; i++) { fm1_env_tick(&fast); fm1_env_tick(&slow); }
        printf("    after 1 s: attack 20 -> %d%%, attack 100 -> %d%%\n",
               100 * fast.level / FM1_ENV_ONE, 100 * slow.level / FM1_ENV_ONE);
        ok(fast.level > slow.level, "a higher ATTACK is a slower attack",
           "the rate table is backwards or flat");
    }

    /* The reason this block exists. Closing the gate must not produce a step
     * bigger than the waveform's own slew -- the old `if (!gate) continue`
     * cut mid-waveform, which is a click on every single mute. */
    {
        static fm1::Drone d;
        static int16_t buf[FM1_BLOCK * 2];

        d.Init();
        d.SetGlobal(G_SPREAD, 0);
        for (uint8_t v = 1; v < FM1_VOICES; v++) d.SetGate(v, 0);
        d.SetParam(0, P_FM, 64);
        d.SetParam(0, P_MOD, 64);
        d.SetParam(0, P_LEVEL, 127);
        d.SetParam(0, P_RELEASE, 30);
        d.NoteOn(0, 45);

        int32_t natural = 0;
        /* A test rig sets a patch, it does not turn a knob: without this the
     * slewed parameters are still on their way when the render below
     * samples them, and every level- or pan-dependent assertion reads a
     * value nobody set. The firmware does the same after a patch load. */
    d.SnapParams();
        for (int b = 0; b < 12; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
            if (b >= 8) {
                int32_t m = max_step(buf, FM1_BLOCK);
                if (m > natural) natural = m;
            }
        }

        d.SetGate(0, 0);
        int32_t release = 0;
        for (int b = 0; b < 8; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
            int32_t m = max_step(buf, FM1_BLOCK);
            if (m > release) release = m;
        }
        printf("    largest sample step: sounding %d, releasing %d\n",
               natural, release);

        ok(release <= natural, "closing the gate adds no step of its own",
           "the release is clicking -- it is cutting, not fading");
    }
}

/* Parameter slewing.
 *
 * The assertion that matters is ARRIVAL. A one-pole written as
 * `s += (t - s) >> 3` stops moving once the gap drops below eight, so
 * without a snap the last fraction of every change never lands: the
 * instrument sits permanently a little flat, a little dark, a little quiet.
 * It is invisible from outside because GetParam returns the target, which is
 * exactly what makes it worth a test.
 *
 * This is the same trap the envelope set -- test the STEP, not the distance
 * (SIGNAL_PATH.md section 8) -- and it is why the engine snaps when the step
 * rounds to zero rather than when the gap looks small.
 */
static void test_slew(void)
{
    printf("\nparameter slew\n");

    static fm1::Drone d;
    static int16_t buf[FM1_BLOCK * 2];
    int blocks_to_arrive = -1;

    d.Init();
    d.SetGate(0, 1);
    d.NoteOn(0, 45);

    /* A full-scale jump on a continuous parameter: 0 -> 127. */
    d.SetParam(0, P_LEVEL, 0);
    d.SnapParams();
    d.SetParam(0, P_LEVEL, 127);

    ok(d.GetParam(0, P_LEVEL) == 127,
       "GetParam reports the target immediately", "it reported something else");

    /* The property that matters is not how long it takes but how big the
     * biggest single jump is. A zipper IS a step: smoothing works when no
     * block moves the value far enough to click, however many blocks that
     * takes. Timing is a comfort question; step size is the bug. */
    int biggest = 0, prev = d.CurrentParam(0, P_LEVEL);
    for (int b = 0; b < 400; b++) {
        int cur;
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
        cur = d.CurrentParam(0, P_LEVEL);
        if (cur - prev > biggest) biggest = cur - prev;
        prev = cur;
        if (cur == 127 && blocks_to_arrive < 0) { blocks_to_arrive = b + 1; break; }
    }

    printf("    0 -> 127 arrived after %d blocks (%d ms), biggest step %d/127\n",
           blocks_to_arrive, blocks_to_arrive * 1000 * (int)FM1_BLOCK / 44100,
           biggest);

    ok(blocks_to_arrive > 0, "a slewed parameter ARRIVES at its target",
       "it stalled short -- the shift rounds to zero near the end");
    ok(biggest < 127, "and does not jump there in one block",
       "it is not being slewed at all");
    /* A full-scale jump is the worst case anyone can ask for, and 1/8 of it
     * per block is what a shift of 3 means by definition. The assertion is
     * that nothing exceeds it. */
    ok(biggest <= 16, "no block moves it more than 1/8 of full scale",
       "a step that large is still a click");
    ok(blocks_to_arrive < 60, "and it finishes inside a third of a second",
       "the tail is not being snapped");

    /* An enum must not slide: there is nothing between COMB and RESO, and a
     * filter easing through BANDPASS on the way would be audible nonsense. */
    d.SetParam(0, P_F1_TYPE, F_NOTCH);
    ok(d.CurrentParam(0, P_F1_TYPE) == F_NOTCH,
       "an enum lands immediately, without a block of lag",
       "a mode change was slewed");

    /* And a patch load lands as a unit. */
    d.SetParam(0, P_LEVEL, 10);
    d.SnapParams();
    ok(d.CurrentParam(0, P_LEVEL) == 10,
       "SnapParams lands everything at once", "a snapped value was still moving");
}

/* The LFOs.
 *
 * What is worth pinning is not the shapes -- four lines of arithmetic each --
 * but the two things that would stay invisible until a patch sounded wrong:
 *
 *   - the SLOW end has to actually move. At 0.01 Hz the per-block increment
 *     is a small integer, and a generator that rounded it to zero would give
 *     an LFO sitting perfectly still while claiming a hundred-second cycle.
 *   - the continuous shapes must not JUMP. A triangle that wraps badly is a
 *     click in everything it modulates, and at these rates you would wait a
 *     minute between occurrences while hunting it.
 */
static void test_lfo(void)
{
    printf("\nLFOs\n");

    static fm1_lfo_t l;
    int bounded = 1, i;

    /* Every mode stays inside Q15 at full amount, including the random ones
     * whose endpoints come from a 32-bit generator. */
    for (uint8_t m = 0; m < LFO_NMODES; m++) {
        fm1_lfo_init(&l, 12345u + m);
        fm1_lfo_set_mode(&l, m);
        fm1_lfo_set_amount(&l, 127);
        fm1_lfo_set_rate(&l, 100);
        for (i = 0; i < 20000; i++) {
            int32_t v = fm1_lfo_block(&l);
            if (v > Q15_ONE || v < Q15_MIN) { bounded = 0; break; }
        }
    }
    ok(bounded, "every mode stays inside Q15 at full amount", "wrapped");

    /* Amount 0 is silence, in every mode: an LFO reaching nothing must reach
     * nothing, not almost nothing. */
    {
        int silent = 1;
        for (uint8_t m = 0; m < LFO_NMODES; m++) {
            fm1_lfo_init(&l, 999u + m);
            fm1_lfo_set_mode(&l, m);
            fm1_lfo_set_amount(&l, 0);
            fm1_lfo_set_rate(&l, 90);
            for (i = 0; i < 2000; i++)
                if (fm1_lfo_block(&l) != 0) { silent = 0; break; }
        }
        ok(silent, "amount 0 is exactly zero in every mode", "it leaked");
    }

    /* The slowest rate must still arrive. One block is 5.805 ms, so a 100 s
     * cycle is about 17,000 blocks -- run two cycles' worth and require the
     * thing to have visited both ends of its travel. */
    {
        int32_t lo = 32767, hi = -32768;
        fm1_lfo_init(&l, 7u);
        fm1_lfo_set_mode(&l, LFO_TRI);
        fm1_lfo_set_amount(&l, 127);
        fm1_lfo_set_rate(&l, 0);
        for (i = 0; i < 40000; i++) {
            int32_t v = fm1_lfo_block(&l);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        printf("    slowest rate over 40000 blocks (232 s): %d .. %d\n", lo, hi);
        ok(hi > 20000 && lo < -20000,
           "the slowest rate still traverses its range",
           "the increment rounded to zero: the LFO never moves");
    }

    /* The continuous shapes step smoothly: no block may move more than a
     * fraction of the range, or it is a click in whatever it reaches. */
    {
        static const uint8_t SMOOTH_MODES[] = { LFO_TRI, LFO_SMOOTH };
        int ok_all = 1;
        for (uint32_t k = 0; k < sizeof SMOOTH_MODES; k++) {
            int32_t prev;
            int jumps = 0;
            fm1_lfo_init(&l, 4242u + k);
            fm1_lfo_set_mode(&l, SMOOTH_MODES[k]);
            fm1_lfo_set_amount(&l, 127);
            fm1_lfo_set_rate(&l, 60);
            prev = fm1_lfo_block(&l);
            for (i = 0; i < 20000; i++) {
                int32_t v = fm1_lfo_block(&l);
                int32_t d = v > prev ? v - prev : prev - v;
                if (d > 4000) jumps++;
                prev = v;
            }
            if (jumps) {
                ok_all = 0;
                printf("    mode %u: %d jumps\n", SMOOTH_MODES[k], jumps);
            }
        }
        ok(ok_all, "TRI and SMOOTH never step more than 1/8 of range",
           "a discontinuity -- that is a click in everything it modulates");
    }

    /* S&H holds: within a cycle the value must not change at all. That is the
     * entire difference between it and SMOOTH. */
    {
        int32_t prev;
        int changes = 0;
        fm1_lfo_init(&l, 31337u);
        fm1_lfo_set_mode(&l, LFO_SH);
        fm1_lfo_set_amount(&l, 127);
        fm1_lfo_set_rate(&l, 40);
        prev = fm1_lfo_block(&l);
        for (i = 0; i < 4000; i++) {
            int32_t v = fm1_lfo_block(&l);
            if (v != prev) changes++;
            prev = v;
        }
        printf("    S&H changed %d times in 4000 blocks\n", changes);
        ok(changes > 0 && changes < 400,
           "S&H holds between cycles rather than moving every block",
           "it is not holding, or it never steps at all");
    }
}

/* The modulation matrix.
 *
 * A screen can show a cable and a moving bar without one sample changing, so
 * the assertion that matters is that modulation reaches the AUDIO. Pitch is
 * the destination to prove it on: a frequency change shows up as a change in
 * zero crossings, which needs no spectrum and no reference recording.
 */
static int count_crossings(const int16_t *buf, uint32_t frames)
{
    int n = 0;
    for (uint32_t i = 2; i < frames * 2u; i += 2)       /* left channel */
        if ((buf[i] >= 0) != (buf[i - 2] >= 0)) n++;
    return n;
}

static void crossing_range(fm1::Drone &d, int blocks, int *lo, int *hi)
{
    static int16_t buf[FM1_BLOCK * 2];
    *lo = 1 << 30;
    *hi = 0;
    for (int b = 0; b < blocks; b++) {
        int c;
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
        c = count_crossings(buf, FM1_BLOCK);
        if (c < *lo) *lo = c;
        if (c > *hi) *hi = c;
    }
}

static void test_mod_matrix(void)
{
    printf("\nmodulation matrix\n");

    static fm1::Drone d;
    int lo, hi, steady;

    d.Init();
    d.SetGlobal(G_SPREAD, 0);
    for (uint8_t v = 0; v < FM1_VOICES; v++) d.SetGate(v, v == 0);
    d.SetParam(0, P_SHAPE, 3);              /* SINE_TRIANGLE: clean crossings */
    d.SetParam(0, P_F1_TYPE, F_OFF);
    d.SetParam(0, P_F2_TYPE, F_OFF);
    d.SetParam(0, P_FM, 64);                /* drift off: the matrix alone */
    d.SetParam(0, P_MOD, 64);
    d.SetParam(0, P_LEVEL, 127);
    d.NoteOn(0, 45);
    d.SnapParams();

    /* Unrouted first. If the pitch is not steady to begin with, nothing
     * measured afterwards can be attributed to the matrix. */
    crossing_range(d, 8, &lo, &hi);             /* settle */
    crossing_range(d, 60, &lo, &hi);
    printf("    unrouted:                  crossings %d..%d\n", lo, hi);
    steady = hi - lo;
    ok(steady <= 2, "an unrouted voice holds its pitch",
       "something else is already moving it");

    /* Route LFO1 into pitch at full depth. */
    d.SetGlobal(G_LFO(0, G_LFO_MODE), LFO_TRI);
    d.SetGlobal(G_LFO(0, G_LFO_RATE), 90);
    d.SetGlobal(G_LFO(0, G_LFO_AMT), 127);
    d.SetParam(0, P_MD(MD_PITCH, P_MD_SRC), 1);
    d.SetParam(0, P_MD(MD_PITCH, P_MD_DEPTH), 127);
    d.SnapParams();

    crossing_range(d, 200, &lo, &hi);
    printf("    LFO1 -> PITCH, full depth: crossings %d..%d\n", lo, hi);
    ok(hi - lo > steady * 4 && hi > lo * 5 / 4,
       "modulation reaches the audio",
       "the pitch never moved -- the matrix is drawn but not wired");

    /* And pulling the cable stops it, rather than leaving the voice stuck
     * wherever the LFO happened to be when the source was cleared. */
    d.SetParam(0, P_MD(MD_PITCH, P_MD_SRC), MD_SRC_NONE);
    d.SnapParams();
    crossing_range(d, 8, &lo, &hi);             /* settle */
    crossing_range(d, 60, &lo, &hi);
    printf("    unrouted again:            crossings %d..%d\n", lo, hi);
    ok(hi - lo <= 2, "unrouting stops it", "the modulation is stuck on");

    /* --- the filter destinations ------------------------------------------
     *
     * These take a different path from everything else: a cutoff cannot be
     * read out of a variable at render time, it has to be pushed back
     * through its setter so the coefficient is recomputed -- and in COMB
     * mode the delay tuning with it. So they are worth their own assertion
     * rather than being assumed to work because pitch does.
     *
     * This is also docs/ROADMAP.md's own done-condition for Phase 2, stated
     * as "a slow LFO into comb tune audibly evolves a drone over a minute
     * without touching anything". Measured in amplitude rather than
     * crossings: a comb sweeping its tuning moves peaks and notches across
     * the harmonics, which changes how much gets through. */
    {
        static int16_t buf[FM1_BLOCK * 2];
        int32_t plo = 1 << 30, phi = 0;
        int32_t base_lo, base_hi;

        d.SetParam(0, P_SL(0, P_SL_TYPE), SL_COMB);
        d.SetParam(0, P_SL(0, P_SL_MIX),  127);
        d.SetParam(0, P_SL(0, P_SL_P2),   100);  /* resonant enough to matter */
        d.SetParam(0, P_SL(0, P_SL_P1),   64);
        d.SnapParams();
        for (int b = 0; b < 16; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
        }
        for (int b = 0; b < 60; b++) {
            int32_t p;
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
            p = peak_of(buf, FM1_BLOCK);
            if (p < plo) plo = p;
            if (p > phi) phi = p;
        }
        base_lo = plo;
        base_hi = phi;
        printf("    comb, unrouted:            peak %d..%d\n", base_lo, base_hi);

        d.SetGlobal(G_LFO(1, G_LFO_MODE), LFO_TRI);
        d.SetGlobal(G_LFO(1, G_LFO_RATE), 70);
        d.SetGlobal(G_LFO(1, G_LFO_AMT), 127);
        d.SetParam(0, P_MD(MD_SL1, P_MD_SRC), 2);       /* LFO2 */
        d.SetParam(0, P_MD(MD_SL1, P_MD_DEPTH), 127);
        d.SnapParams();

        plo = 1 << 30; phi = 0;
        for (int b = 0; b < 300; b++) {
            int32_t p;
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
            p = peak_of(buf, FM1_BLOCK);
            if (p < plo) plo = p;
            if (p > phi) phi = p;
        }
        printf("    LFO2 -> COMB TUNE:         peak %d..%d\n", plo, phi);
        ok(phi - plo > (base_hi - base_lo) * 2,
           "a modulated comb tune changes the sound",
           "the filter destinations are not wired through their setters");
    }
}

/* GLIDE, HAAS and drift-as-a-source.
 *
 * These three exist because four parameters were found sitting on pages,
 * addressable over MIDI and saved into patches, wired to nothing at all.
 * The reachability test proves a parameter can be FOUND; only this kind
 * proves turning it does something. Written first for that reason.
 */
static void test_late_additions(void)
{
    printf("\nglide, haas, drift routing\n");

    static fm1::Drone d;
    static int16_t buf[FM1_BLOCK * 2];

    /* --- GLIDE ---------------------------------------------------------- */
    d.Init();
    d.SetGlobal(G_SPREAD, 0);
    for (uint8_t v = 0; v < FM1_VOICES; v++) d.SetGate(v, v == 0);
    d.SetParam(0, P_SHAPE, 3);
    d.SetParam(0, P_F1_TYPE, F_OFF);
    d.SetParam(0, P_F2_TYPE, F_OFF);
    d.SetParam(0, P_FM, 64);
    d.SetParam(0, P_MOD, 64);
    d.SetParam(0, P_GLIDE, 0);
    d.NoteOn(0, 45);
    d.SnapParams();

    {
        int lo, hi, settled, moved = 0;
        crossing_range(d, 8, &lo, &hi);
        crossing_range(d, 20, &lo, &hi);
        settled = hi;

        /* Glide off: an octave jump must land inside one block. */
        d.NoteOn(0, 57);
        crossing_range(d, 2, &lo, &hi);
        printf("    glide 0,   after a +12 jump: crossings %d (was %d)\n",
               hi, settled);
        ok(hi > settled * 3 / 2, "GLIDE 0 arrives immediately",
           "the pitch did not move -- or it is gliding when it should not");

        /* Glide on: the same jump must take time. */
        d.SetParam(0, P_GLIDE, 100);
        d.SnapParams();
        d.NoteOn(0, 45);
        crossing_range(d, 40, &lo, &hi);        /* let it arrive */
        crossing_range(d, 20, &lo, &hi);
        settled = hi;
        d.NoteOn(0, 57);
        crossing_range(d, 2, &lo, &hi);
        printf("    glide 100, after a +12 jump: crossings %d (was %d)\n",
               hi, settled);
        if (hi < settled * 3 / 2) moved = 1;
        ok(moved, "GLIDE delays the arrival",
           "the pitch jumped anyway -- glide is not applied");
    }

    /* --- HAAS ------------------------------------------------------------
     * A delay on one channel decorrelates it from the other. Comparing the
     * two channels sample by sample is enough: identical before, different
     * after, and nothing about the LEVEL changed. */
    {
        int32_t diff_off = 0, diff_on = 0, energy = 0;
        d.Init();
        d.SetGlobal(G_SPREAD, 0);
        for (uint8_t v = 0; v < FM1_VOICES; v++) d.SetGate(v, v == 0);
        d.SetParam(0, P_PAN, 64);               /* centre: L and R identical */
        d.SetParam(0, P_FM, 64);
        d.SetParam(0, P_MOD, 64);
        d.NoteOn(0, 45);
        d.SnapParams();

        for (int b = 0; b < 12; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
        }
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i += 2) {
            int32_t x = buf[i] - buf[i + 1];
            diff_off += x < 0 ? -x : x;
            energy += buf[i] < 0 ? -buf[i] : buf[i];
        }

        d.SetGlobal(G_HAAS, 120);               /* well right of centre */
        d.SnapParams();
        for (int b = 0; b < 12; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
        }
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i += 2) {
            int32_t x = buf[i] - buf[i + 1];
            diff_on += x < 0 ? -x : x;
        }

        printf("    L-R difference: haas off %d, haas on %d (energy %d)\n",
               diff_off, diff_on, energy);
        /* NOT zero, and deliberately so: centre is 64/127 left against
         * 63/127 right, one LSB of asymmetry that keeps the two gains
         * summing to what a panned voice has. That is 1/127 of the signal,
         * so the bound is a fraction of the energy rather than a constant --
         * which is also what makes it survive a change of test note. */
        ok(diff_off < energy / 50,
           "a centred voice is the same in both channels but for one LSB",
           "something is already decorrelating them");
        ok(diff_on > diff_off * 10, "HAAS decorrelates the channels",
           "the delay is not reaching the output");

        /* Both sides, and the centre. A one-directional widener always
         * pushes the image the same way, so two voices using it stack in
         * the same place instead of opening apart -- the bipolar range is
         * the feature, not a convenience. */
        {
            int32_t diff_left = 0, lead_r = 0, lead_l = 0;

            d.SetGlobal(G_HAAS, 8);             /* well LEFT of centre */
            d.SnapParams();
            for (int b = 0; b < 12; b++) {
                for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
                d.Render(buf, FM1_BLOCK);
            }
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i += 2) {
                int32_t x = buf[i] - buf[i + 1];
                diff_left += x < 0 ? -x : x;
            }
            printf("    L-R difference: haas left %d\n", diff_left);
            ok(diff_left > diff_off * 10, "HAAS works on the other side too",
               "only one direction is wired");

            /* And which channel is DELAYED must actually swap.
             *
             * The discriminator is the block right after the line clears: a
             * side change zeroes it, so for the first 29 ms the delayed
             * channel reads silence while the other plays normally. Measured
             * any later, both channels are running continuously and carry
             * the same energy -- which is how the first version of this test
             * managed to report the same sign for both extremes. */
            d.SetGlobal(G_HAAS, 64);            /* centre: clears, no delay */
            d.SnapParams();
            for (int b = 0; b < 12; b++) {
                for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
                d.Render(buf, FM1_BLOCK);
            }
            d.SetGlobal(G_HAAS, 127);           /* hard right, line clears */
            d.SnapParams();
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);           /* the ONE block that shows it */
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i += 2)
                lead_r += (buf[i] < 0 ? -buf[i] : buf[i])
                        - (buf[i + 1] < 0 ? -buf[i + 1] : buf[i + 1]);

            d.SetGlobal(G_HAAS, 64);
            d.SnapParams();
            for (int b = 0; b < 12; b++) {
                for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
                d.Render(buf, FM1_BLOCK);
            }
            d.SetGlobal(G_HAAS, 0);             /* hard left, line clears */
            d.SnapParams();
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i += 2)
                lead_l += (buf[i] < 0 ? -buf[i] : buf[i])
                        - (buf[i + 1] < 0 ? -buf[i + 1] : buf[i + 1]);

            printf("    L-R energy in the first block: +127 %d, 0 %d\n",
                   lead_r, lead_l);
            ok((lead_r > 0) != (lead_l > 0),
               "the two extremes delay opposite channels",
               "both ends of the knob do the same thing");
        }
    }

    /* --- DRIFT as a routable source --------------------------------------
     * Drift used to reach exactly two destinations, through Braids' own FM
     * and MOD attenuverters. Routed to PAN it should move the image, which
     * neither of those could ever do. */
    {
        int32_t bal_lo = 1 << 30, bal_hi = -(1 << 30);
        d.Init();
        d.SetGlobal(G_SPREAD, 0);
        for (uint8_t v = 0; v < FM1_VOICES; v++) d.SetGate(v, v == 0);
        d.SetParam(0, P_FM, 64);                /* the attenuverters closed: */
        d.SetParam(0, P_MOD, 64);               /* only the matrix is active */
        d.SetParam(0, P_DRIFT_RATE, 120);       /* fast, so the test is short */
        d.SetParam(0, P_MD(MD_PAN, P_MD_SRC), MD_SRC_DRIFT);
        d.SetParam(0, P_MD(MD_PAN, P_MD_DEPTH), 127);
        d.NoteOn(0, 45);
        d.SnapParams();

        for (int b = 0; b < 200; b++) {
            int32_t l = 0, r = 0;
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            d.Render(buf, FM1_BLOCK);
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i += 2) {
                l += buf[i] < 0 ? -buf[i] : buf[i];
                r += buf[i + 1] < 0 ? -buf[i + 1] : buf[i + 1];
            }
            if (l + r > 1000) {
                int32_t bal = (l - r) * 100 / (l + r);
                if (bal < bal_lo) bal_lo = bal;
                if (bal > bal_hi) bal_hi = bal;
            }
        }
        printf("    DRIFT -> PAN: balance %d%%..%d%%\n", bal_lo, bal_hi);
        ok(bal_hi - bal_lo > 20, "DRIFT reaches the matrix",
           "routing the drift to pan did not move the image");
    }
}

/* --- fine editing ---------------------------------------------------------
 *
 * These are ENCODERS, so 128 destinations was never a physical limit, only a
 * storage one. tgt is Q8 now: a coarse click is 256, a fine click is 1, and
 * Braids finally receives the width it has always taken.
 */
static void test_fine(void)
{
    fm1::Drone d;
    int i;

    printf("\nfine editing (15-bit targets on an encoder)\n");

    d.Init(1);
    d.SetParam(0, P_TIMBRE, 64);
    d.SnapParams();

    /* A fine click must NOT move the coarse value -- that is the whole
     * point of having one. */
    d.NudgeParam(0, P_TIMBRE, +1, 0);
    ok(d.GetParam(0, P_TIMBRE) == 64,
       "a fine click leaves the coarse value alone", "coarse moved");

    for (i = 0; i < 255; i++) d.NudgeParam(0, P_TIMBRE, +1, 0);
    ok(d.GetParam(0, P_TIMBRE) == 65,
       "256 fine clicks make exactly one coarse step", "not 65");

    /* Coarse after fine: round toward travel, so the click both MOVES and
     * lands on the grid. Centre has to stay reachable -- on every bipolar
     * control, centre is what OFF means. */
    d.Init(1); d.SetParam(0, P_TIMBRE, 64); d.SnapParams();
    for (i = 0; i < 90; i++) d.NudgeParam(0, P_TIMBRE, +1, 0);
    d.NudgeParam(0, P_TIMBRE, +1, 1);
    ok(d.GetParam(0, P_TIMBRE) == 65,
       "a coarse click up from off-grid lands on the grid above", "not 65");

    d.Init(1); d.SetParam(0, P_TIMBRE, 64); d.SnapParams();
    for (i = 0; i < 90; i++) d.NudgeParam(0, P_TIMBRE, +1, 0);
    d.NudgeParam(0, P_TIMBRE, -1, 1);
    ok(d.GetParam(0, P_TIMBRE) == 64,
       "a coarse click down from off-grid lands on the grid below", "not 64");

    /* On the grid it is just a step, with no click swallowed. */
    d.Init(1); d.SetParam(0, P_TIMBRE, 64); d.SnapParams();
    d.NudgeParam(0, P_TIMBRE, +1, 1);
    ok(d.GetParam(0, P_TIMBRE) == 65, "on-grid, a coarse click is one step", "not 65");

    /* Clamping from a fine position, both ends. */
    d.Init(1); d.SetParam(0, P_TIMBRE, 127); d.SnapParams();
    for (i = 0; i < 600; i++) d.NudgeParam(0, P_TIMBRE, +1, 0);
    ok(d.GetParam(0, P_TIMBRE) == 127, "fine cannot push past the maximum", "overran");
    d.SetParam(0, P_TIMBRE, 0); d.SnapParams();
    for (i = 0; i < 600; i++) d.NudgeParam(0, P_TIMBRE, -1, 0);
    ok(d.GetParam(0, P_TIMBRE) == 0, "fine cannot push below zero", "underran");

    /* Offered only where the bits reach something.
     *
     * The cutoffs were excluded at first, on the grounds that fm1_svf_f[] is
     * indexed by MIDI note so seven bits IS its resolution. True of the
     * table and false of the filter: braids::Svf interpolates its frequency
     * and always did, and the one-pole does now too. The conclusion looked at
     * the table instead of at what reads it.
     *
     * So this assertion flipped on purpose, and the reason is written down
     * rather than the expectation quietly edited to match. */
    ok(fm1::Drone::HasFine(P_TIMBRE) && fm1::Drone::HasFine(P_COLOR),
       "TIMBRE and COLOR carry their fine bits", "not offered");
    ok(fm1::Drone::HasFine(P_F1_CUT) && fm1::Drone::HasFine(P_F2_CUT),
       "and the cutoffs, now the filter interpolates them", "not offered");
    /* The envelope TIMES joined too, and this assertion flipped with them.
     * It used to read "an enum, a time and a mode do not", on the reasoning
     * that a time does not need sub-step resolution. That is wrong here for
     * the same reason it was wrong about the cutoffs: fm1_env_a[] sweeps
     * 0.4 ms to 32 s in 128 steps, so near the top one click moves a swell
     * by whole seconds. The range is what makes seven bits coarse, not the
     * kind of thing being measured. */
    ok(fm1::Drone::HasFine(P_ATTACK) && fm1::Drone::HasFine(P_DECAY) &&
       fm1::Drone::HasFine(P_RELEASE) && fm1::Drone::HasFine(P_HOLD),
       "and the envelope times, which span 0.4 ms to 32 s", "not offered");

    /* SUSTAIN is a LEVEL, not a time -- 128 steps across it is plenty, and
     * offering a 1/256 step would be a control that moves nothing audible. */
    ok(!fm1::Drone::HasFine(P_SHAPE) && !fm1::Drone::HasFine(P_SUSTAIN) &&
       !fm1::Drone::HasFine(P_ENV_MODE) && !fm1::Drone::HasFine(P_F1_TYPE),
       "an enum, a level and a mode do not", "offered where it does nothing");

    /* The coarse readout must keep saying what was dialled, through a render
     * and through the slew. */
    d.Init(1); d.SetParam(0, P_TIMBRE, 64); d.SnapParams();
    d.NudgeParam(0, P_TIMBRE, +40, 0);
    {
        int16_t buf[FM1_BLOCK * 2];
        for (i = 0; i < 8; i++) {
            for (int j = 0; j < FM1_BLOCK * 2; j++) buf[j] = 0;
            d.Render(buf, FM1_BLOCK);
        }
    }
    ok(d.GetParam(0, P_TIMBRE) == 64,
       "and the coarse readout still says what you dialled", "coarse drifted");
}

/* --- detune is symmetric about its centre -------------------------------
 *
 * Reported from the panel: 0 -> -1 cent stepped, 0 -> +1 did not, and
 * -1 -> -50 was smooth. A jump at the zero crossing and nowhere else.
 *
 * `(cents * 164) >> 7` is not a divide by 128. An arithmetic right shift
 * rounds toward negative infinity, so it truncates above zero and FLOORS
 * below it: +1 cent gave +1 pitch unit and -1 cent gave -2. The first click
 * below centre moved twice as far as the first click above.
 *
 * The property is simple enough to state exactly, so it is stated exactly.
 */
static void test_detune_symmetry(void)
{
    fm1::Drone d;
    int worst = 0, i;
    int16_t zero;

    printf("\ndetune (symmetric about centre)\n");

    d.Init(1);
    d.SetParam(0, P_OCTAVE, 4);
    d.SetGlobal(G_SPREAD, 0);          /* spread would tilt the voices */
    d.SetParam(0, P_DETUNE, 50);
    d.SnapParams();
    zero = d.PitchBase(0);

    for (i = 1; i <= 50; i++) {
        int up, dn, diff;
        d.SetParam(0, P_DETUNE, (uint8_t)(50 + i)); d.SnapParams();
        up = d.PitchBase(0) - zero;
        d.SetParam(0, P_DETUNE, (uint8_t)(50 - i)); d.SnapParams();
        dn = zero - d.PitchBase(0);
        diff = up > dn ? up - dn : dn - up;
        if (diff > worst) worst = diff;
    }
    printf("  worst up/down asymmetry over +-50 cents: %d pitch units\n", worst);
    ok(worst == 0, "detuning down matches detuning up, cent for cent",
       "the two sides of the centre detent disagree");

    /* And the first click either way is the same size -- the one the panel
     * actually caught. */
    d.SetParam(0, P_DETUNE, 51); d.SnapParams();
    {
        int up = d.PitchBase(0) - zero;
        int dn;
        d.SetParam(0, P_DETUNE, 49); d.SnapParams();
        dn = zero - d.PitchBase(0);
        ok(up == dn && up == 1,
           "the first cent either side is one pitch unit", "not 1 and 1");
    }

    /* The ends still reach +-50 cents: 64 units of 1/128 semitone. */
    d.SetParam(0, P_DETUNE, 100); d.SnapParams();
    ok(d.PitchBase(0) - zero == 64, "+50 cents is 64 pitch units", "span moved");
    d.SetParam(0, P_DETUNE, 0); d.SnapParams();
    ok(zero - d.PitchBase(0) == 64, "-50 cents is 64 pitch units", "span moved");

    /* AND THE FREQUENCY THE SLOTS TUNE FROM, which is the one that actually
     * broke. Symmetric pitch was not enough: PitchToHz quantised to whole
     * semitones, so one cent flat moved the comb an entire semitone while
     * fifty cents sharp moved it not at all. Different harmonics either side
     * of the centre detent, from a control that reads +-50 cents.
     *
     * Walk the whole knob and demand that no single click moves the comb
     * further than a click possibly can. One cent is about 0.058% of a
     * frequency; anything past a tenth of a percent is a boundary jump. */
    {
        int32_t base = (int32_t)(60 * 128);       /* note 60, on the grid */
        uint32_t prev = (uint32_t)fm1::Drone::PitchToHz(base - 64);
        uint32_t worst_num = 0, worst_den = 1;
        int c;

        for (c = -63; c <= 64; c++) {
            uint32_t h = (uint32_t)fm1::Drone::PitchToHz(base + c);
            uint32_t dd = h > prev ? h - prev : prev - h;
            /* compare dd/h against worst_num/worst_den without floats */
            if ((uint64_t)dd * worst_den > (uint64_t)worst_num * h) {
                worst_num = dd; worst_den = h;
            }
            prev = h;
        }
        printf("  worst single-step comb jump: %u/1e6 of the frequency\n",
               (unsigned)((uint64_t)worst_num * 1000000u / worst_den));
        ok((uint64_t)worst_num * 1000u < (uint64_t)worst_den,
           "the comb's tuning never jumps between adjacent detune steps",
           "a step moved the resonator more than a cent could");
    }

    /* THE FILTER TUNING, which had the same shape of fault one table over.
     *
     * The old fm1_svf_f[] was indexed by whole MIDI note, so anything that
     * reached it by flooring a pitch landed on a semitone grid -- the
     * resonator slot did (`key >> 7`, the comb's bug exactly) and so did the
     * 6 dB branch.
     *
     * Everything is trapezoidal now, and fm1_tpt_g interpolates within the
     * semitone, so this asks the same question of the tuning that actually
     * ships: no adjacent step may move the coefficient further than one
     * pitch unit possibly can. */
    {
        uint32_t prev = (uint32_t)fm1_tpt_g(60 * 128 - 64);
        uint32_t wn = 0, wd = 1;
        int c;
        for (c = -63; c <= 64; c++) {
            uint32_t h = (uint32_t)fm1_tpt_g(60 * 128 + c);
            uint32_t dd = h > prev ? h - prev : prev - h;
            if ((uint64_t)dd * wd > (uint64_t)wn * h) { wn = dd; wd = h; }
            prev = h;
        }
        printf("  worst single-step filter jump: %u/1e6 of the coefficient\n",
               (unsigned)((uint64_t)wn * 1000000u / wd));
        ok((uint64_t)wn * 1000u < (uint64_t)wd,
           "the filter corner never jumps between adjacent cutoff steps",
           "a step moved the corner more than a pitch unit could");
    }

    /* And it must still be RIGHT, not merely smooth: A4 is 440 Hz. */
    {
        uint32_t a4 = (uint32_t)fm1::Drone::PitchToHz(69 * 128);
        ok(a4 > (440u << 16) - 2000u && a4 < (440u << 16) + 2000u,
           "note 69 is still 440 Hz", "the interpolation moved concert pitch");
    }
}


/* --- the mix buses -------------------------------------------------------
 *
 * Four channels that sum whatever points at them, apply a level and a
 * BALANCE, and go to the main output. No destination of their own, so the
 * only thing that can go wrong is silently: a bus that drops its input, or
 * one that scales it wrongly, sounds like a routing mistake rather than a
 * bug.
 */
static int32_t bus_peak(uint8_t dest, uint8_t bus_level, uint8_t bus_pan,
                        int side)
{
    fm1::Drone d;
    int16_t buf[FM1_BLOCK * 2];
    int32_t pk = 0;

    d.Init();
    d.SetGlobal(G_SPREAD, 0);
    for (uint8_t v = 1; v < FM1_VOICES; v++) d.SetGate(v, 0);
    d.SetParam(0, P_SHAPE, braids::MACRO_OSC_SHAPE_CSAW);
    d.SetParam(0, P_LEVEL, 127);
    d.SetParam(0, P_PAN,   64);
    d.SetParam(0, P_DEST,  dest);
    d.SetGlobal(G_MIX(0, G_MIX_LEVEL), bus_level);
    d.SetGlobal(G_MIX(0, G_MIX_PAN),   bus_pan);
    d.NoteOn(0, 45);
    d.SnapParams();

    for (int b = 0; b < 40; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
        for (uint32_t i = (side < 0 ? 0u : 1u); i < FM1_BLOCK * 2u; i += 2u) {
            int32_t a = buf[i] < 0 ? -(int32_t)buf[i] : (int32_t)buf[i];
            if (a > pk) pk = a;
        }
    }
    return pk;
}

static void test_mix_buses(void)
{
    int32_t direct, viabus, half, quiet;

    printf("\nmix buses\n");

    direct = bus_peak(DEST_OUT,  127, 64, -1);
    viabus = bus_peak(DEST_MIX1, 127, 64, -1);
    printf("    voice -> OUT:        %6d\n", direct);
    printf("    voice -> MIX1 (127): %6d\n", viabus);

    ok(viabus > direct * 9 / 10,
       "a voice routed to a bus still reaches the output",
       "the bus swallowed it");

    /* BALANCE, not pan: a bus already carries a stereo image, so its centre
     * must be unity rather than the 0.707 a mono voice gets placed at. */
    ok(viabus > direct * 9 / 10 && viabus < direct * 11 / 10,
       "a bus at centre is unity, not constant-power",
       "the bus pan law is attenuating the centre");

    half = bus_peak(DEST_MIX1, 64, 64, -1);
    ok(half < viabus * 6 / 10 && half > viabus * 4 / 10,
       "the bus level scales what passes through it", "it does not");

    /* Hard right: the left channel should lose the voice. */
    quiet = bus_peak(DEST_MIX1, 127, 127, -1);
    ok(quiet < viabus / 8, "balance hard over empties the far channel",
       "the far channel still has signal");

    /* Independence, stated the right way round. The first draft of this
     * asserted that a voice on MIX2 is NOT heard while bus 1 is open, which
     * is simply false -- it goes out through bus 2. What has to hold is that
     * bus 1's controls do not reach bus 2: with bus 1 shut completely, a
     * voice on bus 2 is still at full level. */
    {
        int32_t other = bus_peak(DEST_MIX2, 0, 64, -1);
        ok(other > direct * 7 / 10,
           "shutting one bus does not touch another",
           "the buses share a level");
    }
}


/* --- parallel cables -----------------------------------------------------
 *
 * Four outputs per voice, each with a destination and a gain. The point is
 * that one voice can be in two places at once -- a reverb and the dry mix --
 * which a single cable could not express at all.
 */
static int32_t cables_peak(const uint8_t *dest, const uint8_t *gain)
{
    fm1::Drone d;
    int16_t buf[FM1_BLOCK * 2];
    int32_t pk = 0;

    d.Init();
    d.SetGlobal(G_SPREAD, 0);
    for (uint8_t v = 1; v < FM1_VOICES; v++) d.SetGate(v, 0);
    d.SetParam(0, P_SHAPE, braids::MACRO_OSC_SHAPE_CSAW);
    /* Deliberately NOT full level. Two cables to the same place sum, and at
     * LEVEL 127 the pair lands on the soft clipper -- the first version of
     * this test read 1.79x instead of 2.0x and called the cables broken
     * when what it had actually measured was the limiter doing its job. */
    d.SetParam(0, P_LEVEL, 56);
    d.SetParam(0, P_PAN,   64);
    for (uint8_t c = 0; c < FM1_NODE_OUTS; c++) {
        d.SetParam(0, (uint8_t)P_OUT(c, P_OUT_DEST), dest[c]);
        d.SetParam(0, (uint8_t)P_OUT(c, P_OUT_GAIN), gain[c]);
    }
    d.NoteOn(0, 45);
    d.SnapParams();

    for (int b = 0; b < 40; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) {
            int32_t a = buf[i] < 0 ? -(int32_t)buf[i] : (int32_t)buf[i];
            if (a > pk) pk = a;
        }
    }
    return pk;
}

static void test_cables(void)
{
    uint8_t dst[FM1_NODE_OUTS], gn[FM1_NODE_OUTS];
    int32_t one, two, half, off;

    printf("\nparallel cables\n");

    dst[0] = DEST_OUT; dst[1] = DEST_OFF; dst[2] = DEST_OFF; dst[3] = DEST_OFF;
    gn[0] = gn[1] = gn[2] = gn[3] = 127;
    one = cables_peak(dst, gn);

    /* The same voice down two cables, both to the output: twice the signal,
     * because it is literally the same samples added to the same bus. */
    dst[1] = DEST_OUT;
    two = cables_peak(dst, gn);
    printf("    one cable:  %6d\n", one);
    printf("    two cables: %6d\n", two);
    ok(two > one * 18 / 10 && two < one * 22 / 10,
       "a second cable to the same place doubles it",
       "the extra cable is not carrying");

    /* And its gain is its own. */
    gn[1] = 64;
    half = cables_peak(dst, gn);
    ok(half > one * 13 / 10 && half < one * 17 / 10,
       "each cable has its own gain", "the gain is shared or ignored");

    /* Unplugged means unplugged, whatever the gain says. */
    dst[1] = DEST_OFF; gn[1] = 127;
    off = cables_peak(dst, gn);
    ok(off > one * 9 / 10 && off < one * 11 / 10,
       "--- unplugs the cable regardless of its gain",
       "an unplugged cable is still being heard");

    /* Cable 1 off, cable 3 on: the cables are independent, not a chain that
     * stops at the first gap. */
    dst[0] = DEST_OFF; dst[2] = DEST_OUT;
    ok(cables_peak(dst, gn) > one * 9 / 10,
       "a later cable works with an earlier one unplugged",
       "the cables are being treated as a list that ends at the first gap");
}


/* --- fine editing, as a BEHAVIOUR ----------------------------------------
 *
 * The existing fine test asserts which parameters Drone::HasFine names. That
 * is necessary and insufficient, and the gap shipped: ATTACK, HOLD, DECAY
 * and RELEASE were added to the list while fm1_env_set_attack still took a
 * uint8_t, so SHIFT offered a sub-step on a control that quantised to the
 * byte. The list said yes; nothing moved.
 *
 * So this one measures. Two envelopes a single fine step apart have to take
 * a DIFFERENT number of samples to get there, or the gesture is decoration.
 */
static long attack_samples(int32_t q8)
{
    fm1_env_t e;
    long n = 0;

    fm1_env_init(&e);
    fm1_env_set_attack_q8(&e, q8);
    fm1_env_set_sustain(&e, 127);
    fm1_env_gate(&e, 1);
    while (e.stage == ENV_ATTACK && n < 44100L * 60) { fm1_env_tick(&e); n++; }
    return n;
}

static void test_fine_moves_something(void)
{
    printf("\nfine editing actually moves the engine\n");

    /* Mid-knob, where one coarse step is a few tens of milliseconds and a
     * 1/256 step is well inside what the integer rate can represent. */
    {
        long a = attack_samples(80 << 8);
        long b = attack_samples((80 << 8) + 128);   /* half a coarse step */
        long c = attack_samples(81 << 8);
        printf("    ATTACK 80 = %ld, 80.5 = %ld, 81 = %ld samples\n", a, b, c);
        ok(b != a, "half a step of ATTACK changes the attack time",
           "the fine bits are being discarded");
        ok(b > a && b < c,
           "and lands between the two coarse steps either side",
           "the interpolation is not monotonic");
    }

    /* The whole point of fine here: at the slow end one coarse step is
     * SECONDS, which is why seven bits was too few to begin with. */
    {
        long a = attack_samples(120 << 8);
        long b = attack_samples((120 << 8) + 128);
        printf("    ATTACK 120 = %.2f s, 120.5 = %.2f s\n",
               (double)a / 44100.0, (double)b / 44100.0);
        ok(b != a, "and at the slow end, where a coarse step is seconds",
           "the fine bits are being discarded");
    }
}


/* --- the comb's feedback SIGN --------------------------------------------
 *
 * fm1_comb_set_feedback has always taken a signed coefficient and clamped it
 * at +-31000, but the slot only ever sent it the positive half -- so half
 * the comb was implemented and unreachable from the panel.
 *
 * The sign is not a subtler version of the same sound. Positive resonates at
 * every multiple of 1/delay; negative inverts each pass, so even multiples
 * cancel and the peaks land between them.
 */
static double comb_gain(int32_t fb, double delay, double hz)
{
    fm1_comb_t c;
    double ph = 0.0, step = 2.0 * 3.14159265358979 * hz / 44100.0;
    int32_t pk = 0;

    fm1_comb_init(&c);
    c.delay_q8 = (uint32_t)(delay * 256.0);
    fm1_comb_set_feedback(&c, fb);
    for (int i = 0; i < 60000; i++) {
        int32_t x = (int32_t)(8000.0 * sin(ph));
        ph += step;
        int32_t y = fm1_comb_tick(&c, (q15_t)x);
        if (i > 40000) { int32_t a = y < 0 ? -y : y; if (a > pk) pk = a; }
    }
    return (double)pk / 8000.0;
}

static void test_comb_polarity(void)
{
    const double D = 100.0, f0 = 44100.0 / D;
    double pos_h, neg_h, pos_m, neg_m;

    printf("\ncomb feedback polarity\n");
    pos_h = comb_gain( 29491, D, f0);          /* on the fundamental */
    neg_h = comb_gain(-29491, D, f0);
    pos_m = comb_gain( 29491, D, f0 * 0.5);    /* halfway to it */
    neg_m = comb_gain(-29491, D, f0 * 0.5);
    printf("    at %4.0f Hz:  + %.2fx   - %.2fx\n", f0, pos_h, neg_h);
    printf("    at %4.0f Hz:  + %.2fx   - %.2fx\n", f0 * 0.5, pos_m, neg_m);

    ok(pos_h > neg_h * 4.0,
       "positive feedback peaks on the harmonic", "it does not");
    ok(neg_m > pos_m * 4.0,
       "negative feedback peaks between them instead", "the sign does nothing");

    /* The slot has to be able to ASK for it, which is the part that was
     * missing: P2 is bipolar about 64 now. */
    {
        fm1_slot_t a, b;
        fm1_slot_init(&a); fm1_slot_init(&b);
        fm1_slot_set_type(&a, SL_COMB); fm1_slot_set_type(&b, SL_COMB);
        fm1_slot_set_p2(&a, 127);
        fm1_slot_set_p2(&b, 0);
        ok(a.comb.fb > 0 && b.comb.fb < 0,
           "and the slot can reach both signs", "P2 is still unipolar");
        fm1_slot_set_p2(&a, 64);
        ok(a.comb.fb == 0, "with the centre detent at no feedback",
           "64 is not neutral");
    }
}


/* --- fine editing on GLOBAL parameters -----------------------------------
 *
 * It was voice-only, which meant the effect rack's cutoff sat on whole knob
 * steps while the identical trapezoidal filter inside a voice had sub-step
 * resolution -- the same filter, coarse on one side of the instrument
 * because fine editing had been written for the other table.
 */
static void test_fine_global(void)
{
    printf("\nfine editing reaches the globals\n");

    ok(fm1::Drone::HasFineGlobal(G_FX(0, G_FX_P1)) &&
       fm1::Drone::HasFineGlobal(G_FX(3, G_FX_P2)),
       "an effect's P1 and P2 carry a fraction", "they do not");
    ok(fm1::Drone::HasFineGlobal(G_LFO(2, G_LFO_RATE)),
       "and an LFO's rate", "it does not");
    ok(!fm1::Drone::HasFineGlobal(G_FX(0, G_FX_TYPE)) &&
       !fm1::Drone::HasFineGlobal(G_LFO(0, G_LFO_MODE)) &&
       !fm1::Drone::HasFineGlobal(G_MASTER),
       "an enum and a plain level do not", "offered where it does nothing");

    /* And it MOVES something -- the rule this list has broken twice. */
    {
        fm1::fx_chan_t a, b;
        fm1::fx_chan_init(&a); fm1::fx_chan_init(&b);
        fm1::fx_chan_set_type(&a, FX_LP); fm1::fx_chan_set_type(&b, FX_LP);
        fm1::fx_chan_set_p1_q8(&a, 80 << 8);
        fm1::fx_chan_set_p1_q8(&b, (80 << 8) + 128);
        printf("    FX cutoff 80 -> g %d, 80.5 -> g %d\n",
               a.tpt[0].a2, b.tpt[0].a2);
        ok(a.tpt[0].a2 != b.tpt[0].a2,
           "half a step of FX cutoff retunes the filter",
           "the fraction is being discarded");
    }
    {
        fm1_lfo_t a, b;
        fm1_lfo_init(&a, 1); fm1_lfo_init(&b, 1);
        fm1_lfo_set_rate_q8(&a, 64 << 8);
        fm1_lfo_set_rate_q8(&b, (64 << 8) + 128);
        ok(a.inc != b.inc, "and half a step of LFO rate changes the rate",
           "the fraction is being discarded");
    }
}


/* --- the four modulation envelopes ---------------------------------------
 *
 * They exist because the matrix needed a source that is not the amplitude.
 * The VCA envelope could already be routed, but it IS the level, so opening
 * a filter with it meant shaping the volume to match -- one envelope, two
 * jobs, and they disagree.
 */
static void test_mod_envs(void)
{
    fm1::Drone d;
    int16_t buf[FM1_BLOCK * 2];
    int32_t rose = 0, fell = 0, idle_at_rest;

    printf("\nmodulation envelopes\n");
    d.Init();

    /* Free-running, so it should climb on its own with nothing played. */
    d.SetGlobal(G_EN(0, G_EN_TRIG), ENT_FREE);
    d.SetGlobal(G_EN(0, G_EN_ATK),  60);
    d.SetGlobal(G_EN(0, G_EN_SUS),  127);
    d.SnapParams();
    for (int b = 0; b < 60; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
        d.Render(buf, FM1_BLOCK);
    }
    rose = d.EnvValue(0);
    printf("    EN1 free-running after 60 blocks: %d / 32767\n", rose);
    ok(rose > 30000, "a FREE envelope runs without a note",
       "it never started");

    /* One following a voice: silent until that voice is gated. */
    {
        fm1::Drone e;
        e.Init();
        e.SetGlobal(G_EN(1, G_EN_TRIG), ENT_V3);
        e.SetGlobal(G_EN(1, G_EN_ATK),  40);
        for (uint8_t v = 0; v < FM1_VOICES; v++) e.SetGate(v, 0);
        e.SnapParams();
        for (int b = 0; b < 40; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            e.Render(buf, FM1_BLOCK);
        }
        idle_at_rest = e.EnvValue(1);
        ok(idle_at_rest < 1000, "one bound to a voice waits for it",
           "it ran with the voice closed");

        e.NoteOn(2, 45);                /* voice 3 */
        e.SetGate(2, 1);
        for (int b = 0; b < 60; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            e.Render(buf, FM1_BLOCK);
        }
        printf("    EN2 bound to V3: %d before the gate, %d after\n",
               idle_at_rest, (int)e.EnvValue(1));
        ok(e.EnvValue(1) > 20000, "and runs when it opens", "it never started");
    }

    /* And it reaches a destination: the same envelope on a filter cutoff
     * has to move the cutoff, which is the whole reason these exist. */
    {
        fm1::Drone f;
        f.Init();
        f.SetGlobal(G_EN(2, G_EN_TRIG), ENT_FREE);
        f.SetGlobal(G_EN(2, G_EN_ATK),  50);
        f.SetParam(0, P_F1_TYPE, F_LP);
        f.SetParam(0, P_F1_CUT,  30);
        f.SetParam(0, P_MD(MD_F1CUT, P_MD_SRC),   MD_SRC_EN1 + 2);
        f.SetParam(0, P_MD(MD_F1CUT, P_MD_DEPTH), 127);
        f.NoteOn(0, 45);
        f.SnapParams();
        for (int b = 0; b < 80; b++) {
            for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) buf[i] = 0;
            f.Render(buf, FM1_BLOCK);
        }
        fell = f.EnvValue(2);
        ok(fell > 20000, "EN3 routed to a cutoff is live", "it is not running");
    }
}

int main(void)
{
    test_soft_clip();
    test_bus();
    test_mix_buses();
    test_cables();
    test_comb_polarity();
    test_mod_envs();
    test_rack();
    test_meters();
    test_eq();
    test_saturator();
    test_env();
    test_slew();
    test_lfo();
    test_mod_matrix();
    test_late_additions();
    test_fine();
    test_fine_moves_something();
    test_fine_global();
    test_detune_symmetry();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
