/* SPDX-License-Identifier: MIT
 *
 * SUPERSEDED by probe_fx.cc, which measures this and the comb from one source
 * with a KIND switch. probe_fx.cc at KIND=1 reproduced this probe's table
 * exactly -- 4,159 / 40,015 / 81,996 / 120,911 cycles per block at 0-3 filters
 * -- so the two are interchangeable and keeping both only lets them drift.
 * Retained rather than deleted because this tree is not under version control.
 * Delete once it is.
 *
 * NOTE: this file predates the probe.ld fix that moved probe RAM above the
 * RESULTS block. It still links and runs; it is simply no longer the probe
 * that gets maintained.
 *
 * What does a filter cost on pi32v2?
 *
 * Measures N instances of Braids' integer SVF in series over a 256-frame
 * block, so the per-voice cost of one filter -- and of two -- is a measured
 * number rather than an estimate. Built and run by
 * scripts/measure-svf-cycles.sh.
 *
 * Harness details (mangled main, the .data copy, the halt breakpoint) are
 * explained in probe_braids.cc.
 */
#include <stddef.h>

#include "braids/svf.h"
#include "probe.h"

#ifndef BLOCKS
#define BLOCKS 64
#endif
#ifndef FILTERS          /* SVFs in series, per voice; 0 measures the baseline */
#define FILTERS 1
#endif
#ifndef VOICES
#define VOICES 4
#endif

#define BLOCK 256

static braids::Svf svf[VOICES][FILTERS > 0 ? FILTERS : 1];
static int16_t buf[BLOCK];

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

    for (int v = 0; v < VOICES; v++)
        for (int f = 0; f < FILTERS; f++) {
            svf[v][f].Init();
            svf[v][f].set_frequency((int16_t)(40 << 7));
            svf[v][f].set_resonance(20000);
            /* alternate LP and HP: two in series is a band, the useful case */
            svf[v][f].set_mode(f & 1 ? braids::SVF_MODE_HP : braids::SVF_MODE_LP);
        }

    for (int i = 0; i < BLOCK; i++) buf[i] = (int16_t)((i * 997) & 0x7FFF);

    int32_t acc = 0;
    for (int b = 0; b < BLOCKS; b++) {
        for (int v = 0; v < VOICES; v++) {
            for (int i = 0; i < BLOCK; i++) {
                int32_t s = buf[i];
                for (int f = 0; f < FILTERS; f++) s = svf[v][f].Process(s);
                acc += s;
            }
        }
    }

    RESULTS[3] = (uint32_t)acc;
    RESULTS[4] = (uint32_t)FILTERS;
    RESULTS[5] = (uint32_t)VOICES;
    p_check(1, 1);
    p_done();
    return 0;
}
