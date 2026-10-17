/* SPDX-License-Identifier: MIT
 *
 * What does an FX block cost on pi32v2?
 *
 * Generalises probe_svf.cc from "how much is a filter" to "how much is each
 * block type", because the block list for the voice filter and the global
 * rack (SIGNAL_PATH.md, OPEN-5) should be chosen from measurements rather
 * than from guesses about which block feels expensive.
 *
 * Measures N instances in series, per voice, over a 256-frame block. Build and
 * run with scripts/measure-fx-cycles.sh. KIND=1 reproduces probe_svf.cc.
 *
 * Harness details (mangled main, the .data copy, the halt breakpoint) are
 * explained in probe_braids.cc.
 *
 * A note on what the numbers mean. The SVF is pure register arithmetic once
 * its coefficients have settled, so the emulator's flat one-instruction-per-
 * cycle model tracks it closely. The comb is a 4 KiB delay line read at two
 * taps per sample, so it is the block most likely to diverge on real silicon:
 * treat its figure as a floor, not a ceiling.
 */
#include <stddef.h>

#include "braids/svf.h"
#include "probe.h"

#include "../engine/fm1_comb.h"

#ifndef BLOCKS
#define BLOCKS 32
#endif
#ifndef KIND             /* 0 = baseline (loop only), 1 = SVF, 2 = comb */
#define KIND 0
#endif
#ifndef N                /* instances in series, per voice */
#define N 1
#endif
#ifndef VOICES
#define VOICES 4
#endif

#define BLOCK 256
#define NN (N > 0 ? N : 1)      /* arrays are never zero-length */

#if KIND == 1
static braids::Svf   blk[VOICES][NN];
#elif KIND == 2
static fm1_comb_t    blk[VOICES][NN];
#endif

static int16_t buf[BLOCK];

/* probe.ld now places RAM above the RESULTS block, so state can no longer
 * collide with it at any size -- but it is still bounded by the region's
 * 0x37FC0 bytes. Fail at compile time, naming the cause, rather than at link
 * time with a region-overflow message. */
#if KIND != 0
static_assert(sizeof(blk) + sizeof(buf) < 0x37FC0,
              "probe state exceeds the RAM region -- lower VOICES or N");
#endif

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

#if KIND == 1
    for (int v = 0; v < VOICES; v++)
        for (int f = 0; f < N; f++) {
            blk[v][f].Init();
            blk[v][f].set_frequency((int16_t)(40 << 7));
            blk[v][f].set_resonance(20000);
            /* alternate LP and HP: two in series is a band, the useful case */
            blk[v][f].set_mode(f & 1 ? braids::SVF_MODE_HP : braids::SVF_MODE_LP);
        }
#elif KIND == 2
    for (int v = 0; v < VOICES; v++)
        for (int f = 0; f < N; f++) {
            fm1_comb_init(&blk[v][f]);
            /* ~110 Hz, which is the drone register the comb is actually used
             * in: inc = 2^32 * 110 / 44100. Each instance in a series pair is
             * detuned so the case is not degenerate. */
            fm1_comb_set_from_inc(&blk[v][f], 10712000u + (uint32_t)f * 400000u, 4096);
            fm1_comb_set_feedback(&blk[v][f], 24000);
        }
#endif

    for (int i = 0; i < BLOCK; i++) buf[i] = (int16_t)((i * 997) & 0x7FFF);

    int32_t acc = 0;
    for (int b = 0; b < BLOCKS; b++) {
        for (int v = 0; v < VOICES; v++) {
            for (int i = 0; i < BLOCK; i++) {
                int32_t s = buf[i];
#if KIND == 1
                for (int f = 0; f < N; f++) s = blk[v][f].Process(s);
#elif KIND == 2
                for (int f = 0; f < N; f++) s = fm1_comb_tick(&blk[v][f], (q15_t)s);
#endif
                acc += s;
            }
        }
    }

    RESULTS[3] = (uint32_t)acc;
    RESULTS[4] = (uint32_t)KIND;
    RESULTS[5] = (uint32_t)N;
    RESULTS[6] = (uint32_t)VOICES;
    p_check(1, 1);
    p_done();
    return 0;
}
