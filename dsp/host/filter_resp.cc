/* SPDX-License-Identifier: MIT
 *
 * What does the voice filter actually do, as against what the knob says?
 *
 * Reported from the device: with the 24 dB lowpass, raising CUT from about
 * 1.5 kHz towards the top of its range makes the whole signal QUIETER. A
 * lowpass opening up cannot do that -- its passband gain is 1 at every
 * cutoff -- so either the passband is collapsing or the filter is leaving
 * its stable region.
 *
 * Measures passband gain directly: a sine well below the corner, in, and the
 * steady-state amplitude out. Then sweeps the test tone to find where the
 * response has fallen 3 dB, which is the cutoff the filter is REALLY at.
 */
#include "../engine/fm1_drone.h"      /* pulls fm1_svf.h, which fm1_filter.h needs */

#include <stdio.h>
#include <math.h>

using namespace fm1;

#define SR 44100.0

/* Steady-state gain at one frequency: settle, then peak-detect. */
static double gain_at(uint8_t cut, uint8_t res, uint8_t poles, uint8_t mode,
                      double hz)
{
    fm1_filter_t f;
    double ph = 0.0, step = 2.0 * M_PI * hz / SR;
    int32_t pk = 0;

    fm1_filter_init(&f);
    fm1_filter_set_mode(&f, mode);
    fm1_filter_set_poles(&f, poles);
    fm1_filter_set_cut(&f, cut);
    fm1_filter_set_res(&f, res);

    for (int i = 0; i < 20000; i++) {
        int32_t x = (int32_t)(16000.0 * sin(ph));
        ph += step;
        int32_t y = fm1_filter_tick(&f, (q15_t)x);
        if (i > 12000) {                       /* settled */
            int32_t a = y < 0 ? -y : y;
            if (a > pk) pk = a;
        }
    }
    return (double)pk / 16000.0;
}

/* MIDI note -> Hz, the unit the CUT knob is documented in. */
static double note_hz(int n) { return 440.0 * pow(2.0, (n - 69) / 12.0); }

/* Where the response is 3 dB down, found by scanning the test tone. */
static double corner(uint8_t cut, uint8_t res, uint8_t poles)
{
    double ref = gain_at(cut, res, poles, F_LP, 50.0);
    if (ref <= 0.0) return -1.0;
    for (double hz = 55.0; hz < 20000.0; hz *= 1.03) {
        if (gain_at(cut, res, poles, F_LP, hz) < ref * 0.7079)
            return hz;
    }
    return -1.0;
}

static void sweep(const char *what, uint8_t poles, uint8_t res)
{
    printf("\n%s, RES %u\n", what, res);
    printf("  %-6s %10s %12s %12s %12s\n",
           "CUT", "SAYS Hz", "PASSBAND", "REAL -3dB", "RATIO");
    printf("  %-6s %10s %12s %12s %12s\n",
           "------", "----------", "------------", "------------", "------------");
    for (int cut = 48; cut <= 127; cut += 8) {
        double says = note_hz(cut);
        double pb   = gain_at((uint8_t)cut, res, poles, F_LP, 50.0);
        double real = corner((uint8_t)cut, res, poles);
        printf("  %-6d %10.0f %9.2fx %12.0f %11.2fx\n",
               cut, says, pb, real, real > 0 ? real / says : 0.0);
    }
}

/* What the player actually hears: total energy of a harmonically rich tone
 * through the filter. A lowpass OPENING must never reduce this -- every
 * partial that was passing still passes, and more join it. */
static double rms_saw(uint8_t cut, uint8_t res, uint8_t poles)
{
    fm1_filter_t f;
    double ph = 0.0, step = 110.0 / SR, acc = 0.0;
    int n = 0;

    fm1_filter_init(&f);
    fm1_filter_set_mode(&f, F_LP);
    fm1_filter_set_poles(&f, poles);
    fm1_filter_set_cut(&f, cut);
    fm1_filter_set_res(&f, res);

    for (int i = 0; i < 40000; i++) {
        int32_t x = (int32_t)(16000.0 * (2.0 * ph - 1.0));   /* naive saw */
        ph += step; if (ph >= 1.0) ph -= 1.0;
        int32_t y = fm1_filter_tick(&f, (q15_t)x);
        if (i > 8000) { acc += (double)y * (double)y; n++; }
    }
    return sqrt(acc / n);
}

static void top_end(const char *what, uint8_t poles)
{
    printf("\n%s -- 110 Hz saw in, RMS out, across the top of the knob\n", what);
    printf("  %-6s %10s %12s %10s\n", "CUT", "SAYS Hz", "RMS OUT", "dB");
    printf("  %-6s %10s %12s %10s\n", "------", "----------", "------------", "----------");
    double ref = rms_saw(100, 0, poles);
    for (int cut = 100; cut <= 127; cut += 3) {
        double r = rms_saw((uint8_t)cut, 0, poles);
        printf("  %-6d %10.0f %12.0f %+9.1f\n",
               cut, note_hz(cut), r, 20.0 * log10(r / ref));
    }
}

/* ---- the trapezoidal replacement ---------------------------------------- */

#include "../engine/fm1_tpt.h"

/* The divide helper has to be exact: a1 is the loop solution, and an error
 * there detunes every cutoff. Checked against real 64-bit arithmetic, which
 * the host has and the chip does not. */
static void check_recip(void)
{
    uint32_t worst_at = 0;
    int64_t worst = 0;

    for (uint32_t den = (1u << 24); den < (1u << 27); den += 9973u) {
        int64_t want = ((int64_t)1 << 48) / den;
        int64_t got  = fm1_tpt_recip(den);
        int64_t err  = want > got ? want - got : got - want;
        if (err > worst) { worst = err; worst_at = den; }
    }
    printf("\nfm1_tpt_recip vs exact 64-bit: worst error %lld at den %u\n",
           (long long)worst, worst_at);
}

static double tpt_gain_at(uint8_t cut, uint8_t res, int cascade, double hz)
{
    fm1_tpt_t a, b;
    double ph = 0.0, step = 2.0 * M_PI * hz / SR;
    int32_t pk = 0;

    fm1_tpt_init(&a); fm1_tpt_init(&b);
    fm1_tpt_set_res(&a, res);   fm1_tpt_set_res(&b, res);
    fm1_tpt_set_pitch(&a, cut * 128); fm1_tpt_set_pitch(&b, cut * 128);

    for (int i = 0; i < 20000; i++) {
        int32_t x = (int32_t)(16000.0 * sin(ph));
        ph += step;
        int32_t y = fm1_tpt_tick(&a, x).lp;
        if (cascade) y = fm1_tpt_tick(&b, y).lp;
        if (i > 12000) { int32_t m = y < 0 ? -y : y; if (m > pk) pk = m; }
    }
    return (double)pk / 16000.0;
}

static double tpt_corner(uint8_t cut, uint8_t res, int cascade)
{
    double ref = tpt_gain_at(cut, res, cascade, 50.0);
    if (ref <= 0.0) return -1.0;
    for (double hz = 55.0; hz < 21000.0; hz *= 1.02)
        if (tpt_gain_at(cut, res, cascade, hz) < ref * 0.7079) return hz;
    return -1.0;
}

static void tpt_sweep(const char *what, int cascade)
{
    printf("\nTPT %s, RES 0\n", what);
    printf("  %-6s %10s %12s %12s %10s\n",
           "CUT", "SAYS Hz", "PASSBAND", "REAL -3dB", "RATIO");
    printf("  %-6s %10s %12s %12s %10s\n",
           "------", "----------", "------------", "------------", "----------");
    for (int cut = 48; cut <= 127; cut += 8) {
        double says = note_hz(cut);
        double pb   = tpt_gain_at((uint8_t)cut, 0, cascade, 50.0);
        double real = tpt_corner((uint8_t)cut, 0, cascade);
        printf("  %-6d %10.0f %9.2fx %12.0f %9.2fx\n",
               cut, says, pb, real, real > 0 ? real / says : 0.0);
    }
}

int main(void)
{
    printf("Voice filter, LOWPASS. Passband measured at 50 Hz, gain 1.00 = unity.\n");
    printf("SAYS is what the knob claims (CUT read as a MIDI note).\n");
    check_recip();
    tpt_sweep("12 dB (one stage)", 0);
    tpt_sweep("24 dB (cascaded)",  1);
    sweep("12 dB (FP_12)", FP_12, 0);
    sweep("24 dB (FP_24)", FP_24, 0);
    top_end("24 dB (FP_24)", FP_24);
    top_end("12 dB (FP_12)", FP_12);
    top_end("6 dB  (FP_6)",  FP_6);
    return 0;
}
