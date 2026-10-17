/* SPDX-License-Identifier: MIT
 *
 * A procedural oscillator for the FM-1, integer only.
 *
 * This is deliberately small: it exists to prove the harness (host render,
 * fixed-vs-float comparison, target compile, benchmark) end to end before any
 * real synthesis work starts. The six-operator engine, harmonic-spectrum
 * editor and wavetables come later and plug in at the same seam.
 */
#ifndef FM1_OSC_H
#define FM1_OSC_H

#include "fm1_fixed.h"

/* The implementation is C; the engine and the probes are C++. Without this
 * guard a C++ caller mangles the names and the link fails with an undefined
 * reference to a function that is plainly in the archive. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t phase;   /* full 32 bits span one cycle; wraps for free */
    uint32_t inc;     /* phase added per sample; see fm1_phase_inc() */
} fm1_osc_t;

/* Set the oscillator's pitch. k_samplerate is FM1_K_44100 or FM1_K_48000. */
static inline void fm1_osc_set_freq(fm1_osc_t *o, q16_t freq_q16, uint32_t k_samplerate)
{
    o->inc = fm1_phase_inc(freq_q16, k_samplerate);
}

static inline void fm1_osc_reset(fm1_osc_t *o) { o->phase = 0; }

/* One sample of a band-limited-enough sine, Q15.
 * Table lookup with linear interpolation: the top FM1_SINE_BITS of the phase
 * index the table, the next 16 bits interpolate. */
q15_t fm1_osc_sine(fm1_osc_t *o);

/* A block of samples. The firmware's audio ISR works in blocks of 256 frames,
 * so the block call is the one that matters for the time budget. */
void fm1_osc_sine_block(fm1_osc_t *o, q15_t *out, uint32_t n);

/* Phase-modulated sine: the FM primitive. mod is added to the phase in Q15
 * units of a full cycle, so mod = 32767 shifts by half a cycle. */
q15_t fm1_osc_sine_pm(fm1_osc_t *o, q15_t mod);

#ifdef __cplusplus
}
#endif

#endif /* FM1_OSC_H */
