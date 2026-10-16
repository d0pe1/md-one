/* SPDX-License-Identifier: MIT */
#include "ref_osc.h"
#include <math.h>

/* M_PI is not in standard C; -std=c99 hides it. Define our own rather than
 * loosening the standard, so the harness builds the same way everywhere. */
#define FM1_PI 3.14159265358979323846

void ref_osc_set_freq(ref_osc_t *o, double hz, double samplerate)
{
    o->inc = hz / samplerate;
}

double ref_osc_sine(ref_osc_t *o)
{
    double s = sin(2.0 * FM1_PI * o->phase);
    o->phase += o->inc;
    if (o->phase >= 1.0) o->phase -= 1.0;
    return s;
}

double snr_db(const double *ref, const double *test, uint32_t n)
{
    double sig = 0.0, err = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double d = test[i] - ref[i];
        sig += ref[i] * ref[i];
        err += d * d;
    }
    if (err == 0.0) return INFINITY;
    if (sig == 0.0) return -INFINITY;
    return 10.0 * log10(sig / err);
}
