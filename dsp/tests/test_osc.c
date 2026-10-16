/* SPDX-License-Identifier: MIT
 *
 * Deterministic tests for the shared integer oscillator.
 *
 * The point of these is not coverage, it is to pin down the properties we will
 * rely on once real synthesis starts:
 *
 *   1. tuning     -- the integer phase increment matches the ideal ratio
 *   2. accuracy   -- the Q15 table oscillator stays close to a float sine
 *   3. determinism-- the same seed/parameters give byte-identical output
 *   4. block = per-sample  -- the block path and the sample path agree
 *   5. phase wrap -- no discontinuity when the 32-bit phase rolls over
 *
 * Everything is seeded and fixed; there is no wall-clock or RNG input beyond
 * an explicit seed, so a failure here is always reproducible.
 */
#include "../shared/fm1_osc.h"
#include "../host/ref_osc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR        44100.0
#define SR_U      44100u
#define N         44100u          /* one second */
#define SNR_FLOOR 70.0            /* dB; see note in main() */

static int failures = 0;

static void check(int cond, const char *what, const char *detail)
{
    printf("  [%s] %s%s%s\n", cond ? "ok" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!cond) failures++;
}

/* --- 1. tuning ----------------------------------------------------------- */
static void test_tuning(void)
{
    puts("tuning (integer phase increment vs the ideal ratio)");
    static const uint32_t hz[] = { 20, 55, 220, 440, 1000, 4186, 12000 };

    double worst_cents = 0.0;
    uint32_t worst_hz = 0;
    for (size_t i = 0; i < sizeof hz / sizeof *hz; i++) {
        uint32_t inc   = fm1_phase_inc(fm1_hz(hz[i]), FM1_K_44100);
        double   ideal = (double)hz[i] / SR * 4294967296.0;
        double   cents = 1200.0 * log2((double)inc / ideal);
        if (fabs(cents) > fabs(worst_cents)) { worst_cents = cents; worst_hz = hz[i]; }
    }
    char d[128];
    snprintf(d, sizeof d, "worst %.6f cents at %u Hz", worst_cents, worst_hz);
    /* 0.001 cent is ~1000x below the just-noticeable difference. */
    check(fabs(worst_cents) < 0.001, "phase increment is in tune", d);
}

/* --- 2. accuracy vs a float reference ------------------------------------ */
static void test_accuracy(void)
{
    puts("accuracy (Q15 table + interpolation vs a double sine)");

    double *ref  = malloc(N * sizeof *ref);
    double *test = malloc(N * sizeof *test);

    fm1_osc_t o = {0, 0};
    fm1_osc_set_freq(&o, fm1_hz(440), FM1_K_44100);
    ref_osc_t r = {0.0, 0.0};
    ref_osc_set_freq(&r, 440.0, SR);

    for (uint32_t i = 0; i < N; i++) {
        test[i] = (double)fm1_osc_sine(&o) / 32767.0;
        ref[i]  = ref_osc_sine(&r);
    }

    double snr = snr_db(ref, test, N);
    char d[128];
    snprintf(d, sizeof d, "%.1f dB (floor %.0f dB)", snr, SNR_FLOOR);
    check(snr > SNR_FLOOR, "fixed point tracks the float reference", d);

    free(ref); free(test);
}

/* --- 3. determinism ------------------------------------------------------ */
static void test_determinism(void)
{
    puts("determinism (same parameters -> identical samples)");

    /* A fixed seed drives the parameter choice, so the test explores more than
     * one frequency while staying exactly reproducible run to run. */
    unsigned seed = 20261016u;
    int same = 1;
    for (int trial = 0; trial < 16 && same; trial++) {
        seed = seed * 1103515245u + 12345u;
        uint32_t hz = 20 + (seed >> 16) % 8000;

        int16_t a[512], b[512];
        fm1_osc_t o1 = {0, 0}, o2 = {0, 0};
        fm1_osc_set_freq(&o1, fm1_hz(hz), FM1_K_44100);
        fm1_osc_set_freq(&o2, fm1_hz(hz), FM1_K_44100);
        fm1_osc_sine_block(&o1, a, 512);
        fm1_osc_sine_block(&o2, b, 512);
        if (memcmp(a, b, sizeof a) != 0) same = 0;
    }
    check(same, "16 seeded frequencies reproduce exactly", "seed 20261016");
}

/* --- 4. block path == per-sample path ------------------------------------ */
static void test_block_matches_sample(void)
{
    puts("block path (the ISR uses blocks; it must equal the sample path)");

    int16_t blk[1024], one[1024];
    fm1_osc_t o1 = {0, 0}, o2 = {0, 0};
    fm1_osc_set_freq(&o1, fm1_hz(997), FM1_K_44100);
    fm1_osc_set_freq(&o2, fm1_hz(997), FM1_K_44100);

    /* deliberately ragged block sizes, as a partial final block would be */
    uint32_t done = 0;
    const uint32_t sizes[] = { 256, 256, 200, 56, 256 };
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        fm1_osc_sine_block(&o1, blk + done, sizes[i]);
        done += sizes[i];
    }
    for (uint32_t i = 0; i < done; i++) one[i] = fm1_osc_sine(&o2);

    check(memcmp(blk, one, done * sizeof *blk) == 0,
          "ragged blocks match sample-at-a-time", "1024 samples @ 997 Hz");
}

/* --- 5. phase wrap ------------------------------------------------------- */
static void test_phase_wrap(void)
{
    puts("phase wrap (32-bit accumulator rollover)");

    fm1_osc_t o = {0, 0};
    fm1_osc_set_freq(&o, fm1_hz(440), FM1_K_44100);
    o.phase = 0xFFFFFFFFu - o.inc * 2u;      /* a few samples from rollover */

    int16_t prev = fm1_osc_sine(&o);
    int32_t worst = 0;
    for (int i = 0; i < 8; i++) {
        int16_t s = fm1_osc_sine(&o);
        int32_t d = abs((int32_t)s - (int32_t)prev);
        if (d > worst) worst = d;
        prev = s;
    }
    /* At 440 Hz one sample advances ~0.01 cycle, so neighbouring samples differ
     * by at most a few hundred in Q15. A wrap bug shows up as a jump of ~65535. */
    char d[64];
    snprintf(d, sizeof d, "largest step %d", worst);
    check(worst < 4000, "no discontinuity across the wrap", d);
}

int main(void)
{
    puts("fm1 dsp -- shared oscillator tests\n");
    test_tuning();
    test_accuracy();
    test_determinism();
    test_block_matches_sample();
    test_phase_wrap();

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILED" : "all tests passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
