/* SPDX-License-Identifier: MIT
 *
 * Render the shared (integer) oscillator to a WAV so we can listen to it.
 *
 *   build/render --hz 220 --sec 2 --out out.wav
 *   build/render --hz 220 --pm 110 --index 8000 --out fm.wav
 */
#include "../shared/fm1_osc.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR     44100u
#define BLOCK  256u          /* the firmware's audio ISR block size */

static void usage(void)
{
    fprintf(stderr,
        "usage: render [--hz F] [--sec S] [--pm F] [--index Q15] [--out FILE]\n"
        "  --hz     carrier frequency in Hz           (default 220)\n"
        "  --sec    length in seconds                 (default 2)\n"
        "  --pm     modulator frequency in Hz, 0=off  (default 0)\n"
        "  --index  modulation index in Q15           (default 0)\n"
        "  --out    output WAV path            (default dsp-render.wav)\n");
}

int main(int argc, char **argv)
{
    uint32_t hz = 220, pm_hz = 0, secs = 2;
    int32_t  index_q15 = 0;
    const char *out = "dsp-render.wav";

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int last = (i + 1 >= argc);
        if      (!strcmp(a, "--hz")    && !last) hz         = (uint32_t)strtoul(argv[++i], 0, 0);
        else if (!strcmp(a, "--sec")   && !last) secs       = (uint32_t)strtoul(argv[++i], 0, 0);
        else if (!strcmp(a, "--pm")    && !last) pm_hz      = (uint32_t)strtoul(argv[++i], 0, 0);
        else if (!strcmp(a, "--index") && !last) index_q15  = (int32_t)strtol(argv[++i], 0, 0);
        else if (!strcmp(a, "--out")   && !last) out        = argv[++i];
        else { usage(); return 2; }
    }
    if (!hz || !secs) { usage(); return 2; }

    uint32_t frames = SR * secs;
    int16_t *buf = malloc((size_t)frames * sizeof *buf);
    if (!buf) { fprintf(stderr, "render: out of memory\n"); return 1; }

    fm1_osc_t car = {0, 0}, mod = {0, 0};
    fm1_osc_set_freq(&car, fm1_hz(hz), FM1_K_44100);
    fm1_osc_set_freq(&mod, fm1_hz(pm_hz ? pm_hz : 1), FM1_K_44100);

    if (pm_hz == 0) {
        /* straight sine: exercise the block path, as the firmware does */
        for (uint32_t i = 0; i < frames; i += BLOCK) {
            uint32_t n = (frames - i < BLOCK) ? frames - i : BLOCK;
            fm1_osc_sine_block(&car, buf + i, n);
        }
    } else {
        for (uint32_t i = 0; i < frames; i++) {
            q15_t m = fm1_osc_sine(&mod);
            buf[i] = fm1_osc_sine_pm(&car, (q15_t)fm1_q15_mul(m, (q15_t)index_q15));
        }
    }

    int rc = wav_write(out, buf, frames, SR, 1);
    if (rc == 0)
        printf("%s: %u frames @ %u Hz, carrier %u Hz%s\n",
               out, frames, SR, hz,
               pm_hz ? " (phase-modulated)" : "");
    else
        fprintf(stderr, "render: could not write %s\n", out);

    free(buf);
    return rc ? 1 : 0;
}
