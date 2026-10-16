/* SPDX-License-Identifier: MIT
 *
 * Render a Braids shape to a WAV on the host, at the FM-1's sample rate.
 *
 *   build/render_braids --shape triple_saw --note 36 --timbre 20000 --sec 4
 *
 * This is the vertical slice: the same vendored MI DSP that compiles for
 * pi32v2, driven through our adapter, at 44.1 kHz, so we can hear and measure
 * it long before any firmware exists.
 */
#include "../mi/fm1_braids.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The sample rate the LINKED resource tables were generated for. 44100 for
 * dsp/mi/braids/resources.cc; the reference build passes 96000 and links
 * upstream's tables instead, so the two can be compared spectrally
 * (scripts/compare-braids-ref.sh). Braids' code has no notion of the rate --
 * it lives entirely in the tables -- so this only labels the output. */
#ifndef RENDER_SR
#define RENDER_SR 44100u
#endif

#define SR    RENDER_SR
#define BLOCK 256u          /* the FM-1's audio ISR block */

struct ShapeName { const char *name; braids::MacroOscillatorShape shape; };

/* The sustaining shapes worth auditioning for a drone instrument. */
static const ShapeName kShapes[] = {
    { "triple_saw",      braids::MACRO_OSC_SHAPE_TRIPLE_SAW      },
    { "triple_square",   braids::MACRO_OSC_SHAPE_TRIPLE_SQUARE   },
    { "triple_triangle", braids::MACRO_OSC_SHAPE_TRIPLE_TRIANGLE },
    { "triple_sine",     braids::MACRO_OSC_SHAPE_TRIPLE_SINE     },
    { "triple_ring",     braids::MACRO_OSC_SHAPE_TRIPLE_RING_MOD },
    { "saw_square",      braids::MACRO_OSC_SHAPE_SAW_SQUARE      },
    { "sine_triangle",   braids::MACRO_OSC_SHAPE_SINE_TRIANGLE   },
    { "csaw",            braids::MACRO_OSC_SHAPE_CSAW            },
    { "morph",           braids::MACRO_OSC_SHAPE_MORPH           },
    { "buzz",            braids::MACRO_OSC_SHAPE_BUZZ            },
    { "saw_swarm",       braids::MACRO_OSC_SHAPE_SAW_SWARM       },
    { "harmonics",       braids::MACRO_OSC_SHAPE_HARMONICS       },
    { "bowed",           braids::MACRO_OSC_SHAPE_BOWED           },
    { "blown",           braids::MACRO_OSC_SHAPE_BLOWN           },
    { "fluted",          braids::MACRO_OSC_SHAPE_FLUTED          },
    { "vowel_fof",       braids::MACRO_OSC_SHAPE_VOWEL_FOF       },
    { "vowel",           braids::MACRO_OSC_SHAPE_VOWEL           },
    { "filtered_noise",  braids::MACRO_OSC_SHAPE_FILTERED_NOISE  },
    { "twin_peaks",      braids::MACRO_OSC_SHAPE_TWIN_PEAKS_NOISE},
    { "granular",        braids::MACRO_OSC_SHAPE_GRANULAR_CLOUD  },
    { "plucked",         braids::MACRO_OSC_SHAPE_PLUCKED         },
    { "struck_bell",     braids::MACRO_OSC_SHAPE_STRUCK_BELL     },
    { "wavetables",      braids::MACRO_OSC_SHAPE_WAVETABLES      },
    { "wave_map",        braids::MACRO_OSC_SHAPE_WAVE_MAP        },
    { "wave_line",       braids::MACRO_OSC_SHAPE_WAVE_LINE       },
    { "saw_comb",        braids::MACRO_OSC_SHAPE_SAW_COMB        },
    { "feedback_fm",     braids::MACRO_OSC_SHAPE_FEEDBACK_FM     },
};
static const int kShapeCount = (int)(sizeof kShapes / sizeof *kShapes);

static void usage(void)
{
    fprintf(stderr,
        "usage: render_braids [--shape NAME] [--note N] [--timbre Q15] [--color Q15]\n"
        "                     [--sec S] [--strike MS] [--out FILE] [--list]\n"
        "  --note    MIDI note number (default 36, C2)\n"
        "  --timbre  Braids TIMBRE, 0..32767 (default 16384)\n"
        "  --color   Braids COLOR,  0..32767 (default 16384)\n"
        "  --strike  re-trigger every MS ms; the plucked, struck, bowed and\n"
        "            noise shapes need this or they decay to silence\n");
}

int main(int argc, char **argv)
{
    const char *shape_name = "triple_saw";
    const char *out = "braids.wav";
    int note = 36, timbre = 16384, color = 16384, secs = 4, strike_ms = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int last = (i + 1 >= argc);
        if (!strcmp(a, "--list")) {
            for (int s = 0; s < kShapeCount; s++) printf("%s\n", kShapes[s].name);
            return 0;
        }
        else if (!strcmp(a, "--shape")  && !last) shape_name = argv[++i];
        else if (!strcmp(a, "--note")   && !last) note   = atoi(argv[++i]);
        else if (!strcmp(a, "--timbre") && !last) timbre = atoi(argv[++i]);
        else if (!strcmp(a, "--color")  && !last) color  = atoi(argv[++i]);
        else if (!strcmp(a, "--sec")    && !last) secs   = atoi(argv[++i]);
        else if (!strcmp(a, "--strike") && !last) strike_ms = atoi(argv[++i]);
        else if (!strcmp(a, "--out")    && !last) out    = argv[++i];
        else { usage(); return 2; }
    }

    const ShapeName *sel = 0;
    for (int s = 0; s < kShapeCount; s++)
        if (!strcmp(kShapes[s].name, shape_name)) { sel = &kShapes[s]; break; }
    if (!sel) {
        fprintf(stderr, "render_braids: unknown shape '%s' (--list)\n", shape_name);
        return 2;
    }

    uint32_t frames = SR * (uint32_t)secs;
    int16_t *buf = (int16_t *)malloc((size_t)frames * sizeof *buf);
    if (!buf) { fprintf(stderr, "render_braids: out of memory\n"); return 1; }

    fm1::BraidsVoice v;
    v.Init();
    v.set_shape(sel->shape);
    v.set_pitch(fm1::pitch_from_midi(note));
    v.set_parameters((int16_t)timbre, (int16_t)color);

    /* Strike on the block boundary nearest each interval, which is how the
     * firmware will do it: MIDI note-on is handled in the main loop and the
     * audio ISR sees the flag on its next block. */
    uint32_t strike_frames = (strike_ms > 0)
                           ? (uint32_t)strike_ms * SR / 1000u : 0u;
    uint32_t next_strike = 0;
    int strikes = 0;

    for (uint32_t i = 0; i < frames; i += BLOCK) {
        if (strike_frames && i >= next_strike) {
            v.Strike();
            next_strike += strike_frames;
            strikes++;
        }
        uint32_t n = (frames - i < BLOCK) ? frames - i : BLOCK;
        v.Render(buf + i, n);
    }

    int rc = wav_write(out, buf, frames, SR, 1);
    if (rc == 0) {
        printf("%s: %s, MIDI %d, timbre %d, color %d, %u frames @ %u Hz",
               out, sel->name, note, timbre, color, frames, SR);
        if (strikes) printf(", %d strikes every %d ms", strikes, strike_ms);
        printf("\n");
        if (fm1::shape_is_rate_calibrated(sel->shape))
            printf("  (rate-calibrated shape: envelopes and filters come from the\n"
                   "   44.1 kHz tables in dsp/mi/braids/resources.cc)\n");
    } else {
        fprintf(stderr, "render_braids: could not write %s\n", out);
    }
    free(buf);
    return rc ? 1 : 0;
}
