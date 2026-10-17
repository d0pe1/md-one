/* SPDX-License-Identifier: MIT
 *
 * Measure what a Braids voice costs on the real pi32v2, in the emulator.
 *
 * Built as a flat probe image (references/fm1-emu/probes/harness) and run with
 *   fm1-emu --bin work/probe_braids_xip.bin --entry 0x02000120
 * which reports instructions retired. The emulator clocks the AC791N at
 * 240 MHz and one instruction per cycle, so retired instructions are our
 * cycle estimate.
 *
 * Cost is isolated by building twice with different BLOCKS and subtracting,
 * so table setup, Init() and the harness cancel out:
 *
 *     per_block = (insns(BLOCKS=N) - insns(BLOCKS=0)) / N
 *
 * CAVEAT: 1 insn/cycle is optimistic. Real silicon stalls on XIP flash reads
 * (every one of Braids' lookup tables lives in flash), on RAM contention with
 * the second core, and on interrupts. Treat the result as a lower bound on
 * time, i.e. an upper bound on how many voices fit.
 */
#include <stddef.h>

#include "../mi/fm1_braids.h"

/* references/fm1-emu/probes/harness is on the include path (MIT, Joakim
 * Eriksson): RESULTS, p_check(), p_done() and the crt0/linker script. */
#include "probe.h"

#ifndef BLOCKS
#define BLOCKS 64
#endif
#ifndef SHAPE
#define SHAPE braids::MACRO_OSC_SHAPE_TRIPLE_SAW
#endif

#define BLOCK 256

static fm1::BraidsVoice voice;
static int16_t out[BLOCK];

/* Freestanding C++ odds and ends.
 *
 * MacroOscillator declares a destructor, so an object of static storage
 * duration registers it for exit with __cxa_atexit. Nothing ever exits here
 * and crt0 runs no .init_array, so these stubs just satisfy the linker. */
extern "C" {
void *__dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }

/* The firmware gets these from Dinghy's libc.c; a probe image has no libc.
 * Deliberately naive -- they are not on the measured path (Init only). */
void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    if (p < q) { while (n--) *p++ = *q++; }
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
}

/* This toolchain mangles `main` in C++ mode (_Z4mainv), but crt0.S calls the
 * unmangled symbol. */
extern "C" int main(void);

/* Placed by dsp/target/probe.ld. */
extern "C" {
extern uint32_t _data_start[], _data_end[], _data_load[];
extern uint32_t _bss_start[], _bss_end[];
}

extern "C" int main(void)
{
    /* The probe crt0 does not do this, and Braids needs it: its three
     * fn_table_ dispatch arrays are initialised data. Without the copy they
     * read as zeros and Render() jumps to 0x00000000. */
    {
        uint32_t *d = _data_start;
        const uint32_t *s = _data_load;
        while (d < _data_end) *d++ = *s++;
        for (uint32_t *b = _bss_start; b < _bss_end; b++) *b = 0;
    }

    voice.Init();
    voice.set_shape(SHAPE);
    voice.set_pitch(fm1::pitch_from_midi(36));
    voice.set_parameters(16384, 16384);

    /* The accumulator is written to the results block so the whole render
     * cannot be optimised away. */
    int32_t acc = 0;
    for (int b = 0; b < BLOCKS; b++) {
        voice.Render(out, BLOCK);
        for (int i = 0; i < BLOCK; i++)
            acc += out[i];
    }

    RESULTS[3] = (uint32_t)acc;
    RESULTS[4] = (uint32_t)BLOCKS;
    RESULTS[5] = (uint32_t)out[0];
    p_check(1, 1);          /* one trivial test so the block is well formed */
    p_done();
    return 0;
}
