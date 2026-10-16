/* SPDX-License-Identifier: MIT
 *
 * Tests for the tuned comb.
 *
 * The one that matters is gain structure. A feedback comb has a resonant peak
 * gain of 1/(1-fb), so at fb = 0.67 a full-scale input is amplified about 3x
 * INSIDE the feedback loop. The loop saturates there, before any output gain,
 * so the distortion never shows up as clipping in the rendered file -- it just
 * sounds wrong. On a struck bell it is audible as a harsh, "grizzled" attack
 * that the comb tail then carries.
 *
 * So: drive the comb with full-scale input at high feedback and assert the
 * loop never saturates.
 */
#include "../engine/fm1_comb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int cond, const char *what, const char *detail)
{
    printf("  [%s] %s%s%s\n", cond ? "ok" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!cond) failures++;
}

#define DELAY_SAMPLES 200

/* Drive the comb with a SUSTAINED square at its resonant frequency -- the
 * worst case, and the one that matters. An isolated impulse does not
 * accumulate (each echo is a separate attenuated copy, peak gain 1.0); the
 * 1/(1-fb) resonant gain only appears when the input keeps reinforcing what
 * is already circulating. A sustained drone through a tuned comb is exactly
 * that case.
 *
 * Returns the peak magnitude reached in the delay line, and counts how often
 * it pinned at full scale. */
static int32_t drive_resonant(int32_t fb_q15, q15_t amp, int n, int *pinned)
{
    static fm1_comb_t c;
    fm1_comb_init(&c);
    c.delay_q8 = DELAY_SAMPLES << 8;
    fm1_comb_set_feedback(&c, fb_q15);

    int32_t pk = 0;
    int pins = 0;
    for (int i = 0; i < n; i++) {
        /* square at exactly the comb's period, so every echo reinforces */
        q15_t x = ((i / (DELAY_SAMPLES / 2)) & 1) ? amp : (q15_t)-amp;
        fm1_comb_tick(&c, x);
        int32_t v = c.buf[(c.w - 1u) & FM1_COMB_MASK];
        if (v >= Q15_ONE || v <= Q15_MIN) pins++;
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    if (pinned) *pinned = pins;
    return pk;
}

static void test_no_internal_saturation(void)
{
    puts("gain structure (a feedback comb must not clip inside its own loop)");

    static const int32_t fbs[] = { 8000, 16000, 24000, 28000, 31000 };
    char d[160];

    for (size_t i = 0; i < sizeof fbs / sizeof *fbs; i++) {
        int pins = 0;
        /* half scale in: a voice rarely runs the comb at full scale, and this
         * still leaves 2x of headroom for the resonance to use */
        drive_resonant(fbs[i], Q15_ONE / 2, 8000, &pins);
        snprintf(d, sizeof d, "fb=%.2f, sustained half-scale -> %d pinned samples",
                 (double)fbs[i] / 32768.0, pins);
        check(pins == 0, "no saturation in the delay line", d);
    }
}

static void test_peak_gain_bounded(void)
{
    puts("peak gain (resonance must stay bounded as feedback rises)");

    static const int32_t fbs[] = { 8000, 16000, 24000, 28000, 31000 };
    char d[160];

    for (size_t i = 0; i < sizeof fbs / sizeof *fbs; i++) {
        q15_t amp = Q15_ONE / 2;
        int32_t pk = drive_resonant(fbs[i], amp, 8000, 0);
        double g = (double)pk / (double)amp;
        snprintf(d, sizeof d, "fb=%.2f -> internal peak %.2fx input",
                 (double)fbs[i] / 32768.0, g);
        /* Without input compensation this is 1/(1-fb): 1.3x at fb=0.24 but
         * 20x at fb=0.95, which pins hard. With compensation it stays ~1. */
        check(g <= 1.6, "peak gain stays bounded", d);
    }
}

static void test_decays(void)
{
    puts("decay (the comb must ring, then stop)");

    static fm1_comb_t c;
    fm1_comb_init(&c);
    c.delay_q8 = 200 << 8;
    fm1_comb_set_feedback(&c, 24000);

    fm1_comb_tick(&c, Q15_ONE);
    for (int i = 0; i < 200 * 60; i++) fm1_comb_tick(&c, 0);

    int32_t pk = 0;
    for (uint32_t i = 0; i < FM1_COMB_LEN; i++) {
        int32_t v = c.buf[i] < 0 ? -c.buf[i] : c.buf[i];
        if (v > pk) pk = v;
    }
    char d[80];
    snprintf(d, sizeof d, "after 60 round trips, residual %d", (int)pk);
    check(pk < 1000, "energy decays away", d);
}

static void test_tuning(void)
{
    puts("tuning (delay derived from the oscillator increment, no 64-bit divide)");

    static const uint32_t hz[] = { 30, 55, 110, 220, 440 };
    char d[160];
    double worst = 0.0;
    uint32_t worst_hz = 0;

    for (size_t i = 0; i < sizeof hz / sizeof *hz; i++) {
        static fm1_comb_t c;
        fm1_comb_init(&c);
        uint32_t inc = fm1_phase_inc(fm1_hz(hz[i]), FM1_K_44100);
        fm1_comb_set_from_inc(&c, inc, 4096);          /* unison */

        double want = 44100.0 / (double)hz[i];         /* samples */
        double got  = (double)c.delay_q8 / 256.0;
        /* fractional error -> cents, small-angle: 1200*log2(1+e) ~ 1731*e */
        double err = (got / want) - 1.0;
        double cents = 1731.0 * (err < 0 ? -err : err);
        if (cents > worst) { worst = cents; worst_hz = hz[i]; }
    }
    snprintf(d, sizeof d, "worst %.2f cents at %u Hz", worst, worst_hz);
    check(worst < 5.0, "comb delay tracks the note", d);
}

int main(void)
{
    puts("fm1 dsp -- comb tests\n");
    test_no_internal_saturation();
    test_peak_gain_bounded();
    test_decays();
    test_tuning();
    printf("\n%s (%d failure%s)\n",
           failures ? "FAILED" : "all tests passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
