/* SPDX-License-Identifier: MIT
 *
 * What does an FM operator cost on pi32v2?
 *
 * The question has a known answer from an unusual source: the stock FM-1
 * firmware runs **12 voices of 6 operators** -- 72 simultaneous operators --
 * plus effects, a sequencer and a UI, on this exact silicon. So FM is not
 * merely affordable here, it is what the hardware was built for. This probe
 * establishes what OUR implementation costs, which is the only number we can
 * actually spend.
 *
 * The primitive already existed: fm1_osc_sine_pm() in dsp/shared/fm1_osc.c,
 * written during bootstrap and validated against a double-precision sine by
 * dsp/tests/test_osc.c. Phase modulation is one add before the table lookup,
 * which is the whole of FM.
 *
 * OPS operators in a serial stack: op[OPS-1] modulates op[OPS-2], and so on
 * down to op[0], the carrier. A stack is the worst case -- every operator
 * runs every sample and none can be skipped -- so it brackets the real range
 * from above. DX7 algorithm 32, six parallel carriers, is the other extreme
 * and costs the same in operators but nothing in modulation depth.
 *
 * Envelopes are deliberately NOT included. A DX7 voice has one per operator,
 * and ours is already priced at ~4,700 cycles/voice/block (AHDSR,
 * SIGNAL_PATH.md section 8). Keeping them separate matters because six
 * envelopes may well cost more than six operators, and that would be the
 * interesting half of the answer.
 */
#include <stddef.h>

#include "../shared/fm1_osc.h"
#include "probe.h"

#ifndef BLOCKS
#define BLOCKS 32
#endif
#ifndef OPS                 /* operators in the stack */
#define OPS 6
#endif
#ifndef FEEDBACK            /* 0 = none; a DX7 voice always has it on one op */
#define FEEDBACK 1
#endif
#ifndef VOICES
#define VOICES 4
#endif

#define BLOCK 256

static fm1_osc_t op[VOICES][OPS];
static q15_t     lvl[OPS];
static q15_t     fb[VOICES];

extern "C" {
void *__dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
void *memset(void *d, int c, size_t n)
{ unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s;
  while (n--) *p++ = *q++; return d; }
extern uint32_t _data_start[], _data_end[], _data_load[];
extern uint32_t _bss_start[], _bss_end[];
int main(void);
}

extern "C" int main(void)
{
    { uint32_t *d = _data_start; const uint32_t *s = _data_load;
      while (d < _data_end) *d++ = *s++;
      for (uint32_t *b = _bss_start; b < _bss_end; b++) *b = 0; }

    /* Ratios 1:1, 2:1, 3:1 ... a plain harmonic stack. The ratio changes the
     * phase increment, not the cost; this is about measuring a realistic
     * patch rather than a degenerate one. */
    for (int v = 0; v < VOICES; v++) {
        for (int i = 0; i < OPS; i++) {
            fm1_osc_reset(&op[v][i]);
            fm1_osc_set_freq(&op[v][i], fm1_hz(110u * (uint32_t)(i + 1)),
                             FM1_K_44100);
        }
        fb[v] = 0;
    }
    for (int i = 0; i < OPS; i++) lvl[i] = (q15_t)(20000 - i * 1500);

    int32_t acc = 0;
    for (int b = 0; b < BLOCKS; b++) {
        for (int v = 0; v < VOICES; v++) {
            for (uint32_t n = 0; n < BLOCK; n++) {
                /* Feedback into the deepest modulator: one multiply and one
                 * state variable, the cheapest interesting thing in FM. */
                int32_t m = FEEDBACK ? (((int32_t)fb[v] * 12000) >> 15) : 0;

                for (int i = OPS - 1; i >= 0; i--) {
                    q15_t s = fm1_osc_sine_pm(&op[v][i], (q15_t)m);
                    m = ((int32_t)s * lvl[i]) >> 15;
                }
                fb[v] = (q15_t)m;
                acc += m;
            }
        }
    }

    RESULTS[3] = (uint32_t)acc;
    RESULTS[4] = (uint32_t)OPS;
    RESULTS[5] = (uint32_t)VOICES;
    RESULTS[6] = (uint32_t)FEEDBACK;
    p_check(1, 1);
    p_done();
    return 0;
}
