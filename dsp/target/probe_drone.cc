/* SPDX-License-Identifier: MIT
 *
 * Measure the WHOLE drone engine on the real pi32v2 -- four voices, combs,
 * drift, panning and the stereo mix, exactly as app_audio() will call it.
 *
 * The per-shape figures in docs/BRAIDS_PORT.md measure one bare oscillator.
 * This measures what actually ships, so it also catches the cost of the comb,
 * the per-sample pan and the saturating mix.
 *
 * Built and run by scripts/measure-drone-cycles.sh; see probe_braids.cc for
 * the harness details (mangled main, the .data copy, the halt breakpoint).
 */
#include <stddef.h>

#include "../engine/fm1_drone.h"

#include "probe.h"

#ifndef BLOCKS
#define BLOCKS 64
#endif
#ifndef VOICES_ON
#define VOICES_ON FM1_VOICES
#endif
#ifndef SHAPE
#define SHAPE braids::MACRO_OSC_SHAPE_TRIPLE_SAW
#endif
#ifndef F2_MODE          /* the second filter; F_OFF keeps figures comparable */
#define F2_MODE F_OFF
#endif
/* Global FX rack slots. Both OFF by default, so the headline figure stays
 * comparable with every earlier measurement. The rack is shared, so its cost
 * does NOT scale with VOICES_ON -- which is the whole reason it is here and
 * not in the voice. */
#ifndef RACK1
#define RACK1 FX_NONE
#endif
#ifndef RACK2
#define RACK2 FX_NONE
#endif
/* EQ bands to make active, and DRIVE. Both default off so the headline
 * figure stays comparable with every earlier measurement. */
#ifndef EQ_BANDS_ON
#define EQ_BANDS_ON 0
#endif
#ifndef DRIVE_ON
#define DRIVE_ON 0
#endif
#ifndef COMB_MIX
#define COMB_MIX 64
#endif

static fm1::Drone drone;
static int16_t out[FM1_BLOCK * 2];      /* stereo */

extern "C" {
void *__dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }

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

extern "C" {
extern uint32_t _data_start[], _data_end[], _data_load[];
extern uint32_t _bss_start[], _bss_end[];
}

extern "C" int main(void);

extern "C" int main(void)
{
    /* The probe crt0 copies neither; Braids' fn_table_ dispatch arrays are
     * initialised data and read as zeros without this. */
    {
        uint32_t *d = _data_start;
        const uint32_t *s = _data_load;
        while (d < _data_end) *d++ = *s++;
        for (uint32_t *b = _bss_start; b < _bss_end; b++) *b = 0;
    }

    drone.Init();

    /* Patch the FX in, not just switch it on. A channel with no cable out is
     * silent and costs nothing -- which is correct behaviour and would make a
     * measurement of "the effect is enabled" read as free. So: voices into
     * FX1, FX1 into FX2 if a second effect was asked for, and the last one
     * out. */
    drone.SetGlobal(G_FX(0, G_FX_TYPE), RACK1);
    drone.SetGlobal(G_FX(1, G_FX_TYPE), RACK2);
    if (RACK1 != FX_NONE) {
        drone.SetGlobal(G_FX(0, G_FX_DEST),
                        RACK2 != FX_NONE ? DEST_FX2 : DEST_OUT);
        if (RACK2 != FX_NONE) drone.SetGlobal(G_FX(1, G_FX_DEST), DEST_OUT);
    }
    for (uint8_t v = 0; v < VOICES_ON; v++) {
        drone.SetParam(v, P_SHAPE,       (uint8_t)(SHAPE));
        /* The comb is a filter mode now; F1 in COMB mode is the chain this
         * probe has always measured. COMB_MIX keeps its name as the build
         * knob that selects it. */
        drone.SetParam(v, P_F1_TYPE,     F_COMB);
        drone.SetParam(v, P_F1_MIX,      COMB_MIX);
        drone.SetParam(v, P_F1_RES,      90);
        /* F2 defaults to bypass, so the headline figure stays comparable with
         * every earlier measurement. Build with -DF2_MODE=F_LP to price the
         * second filter. */
        drone.SetParam(v, P_F2_TYPE,     F2_MODE);
        drone.SetParam(v, P_F2_RES,      60);
        /* Through the FX when there is any, straight out otherwise. */
        drone.SetParam(v, P_DEST, RACK1 != FX_NONE ? DEST_FX1 : DEST_OUT);
        for (int b = 0; b < EQ_BANDS_ON; b++)
            drone.SetParam(v, P_EQ(b, P_EQ_GAIN), 100);     /* a boost */
        drone.SetParam(v, P_DRIVE, DRIVE_ON);
        drone.SetParam(v, P_DRIFT_RATE,  20);
        /* P_DRIFT_DEPTH was split into two attenuverters when drift gained a
         * pitch path: P_FM is drift -> pitch, P_MOD is drift -> timbre, both
         * bipolar with 64 as no modulation. This probe went stale at that
         * change and stopped compiling, so the figures attributed to it came
         * from binaries built before the split, with drift reaching timbre
         * only. Driving both is the heavier and more honest case. */
        drone.SetParam(v, P_FM,          48);
        drone.SetParam(v, P_MOD,         48);
        drone.SetParam(v, P_PAN,         (uint8_t)(20 + v * 29));
        drone.NoteOn(v, (uint8_t)(29 + v * 7));
    }

    int32_t acc = 0;
    for (int b = 0; b < BLOCKS; b++) {
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) out[i] = 0;
        drone.Render(out, FM1_BLOCK);
        for (uint32_t i = 0; i < FM1_BLOCK * 2u; i++) acc += out[i];
    }

    RESULTS[3] = (uint32_t)acc;
    RESULTS[4] = (uint32_t)BLOCKS;
    RESULTS[5] = (uint32_t)VOICES_ON;
    p_check(1, 1);
    p_done();
    return 0;
}
