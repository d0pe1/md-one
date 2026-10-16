/* SPDX-License-Identifier: MIT
 *
 * Floating-point reference oscillator -- HOST ONLY.
 *
 * This file must never be compiled for the target: a single double would fail
 * the link (no FPU, no soft-float). It exists so dsp/tests can measure what the
 * integer implementation in dsp/shared costs in accuracy.
 */
#ifndef FM1_HOST_REF_OSC_H
#define FM1_HOST_REF_OSC_H

#include <stdint.h>

typedef struct {
    double phase;   /* cycles, 0..1 */
    double inc;     /* cycles per sample */
} ref_osc_t;

void   ref_osc_set_freq(ref_osc_t *o, double hz, double samplerate);
double ref_osc_sine(ref_osc_t *o);

/* Signal-to-noise ratio in dB of `test` against `ref`, both length n.
 * Returns +INFINITY when they are identical. */
double snr_db(const double *ref, const double *test, uint32_t n);

#endif
