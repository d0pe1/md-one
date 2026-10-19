/* SPDX-License-Identifier: MIT
 *
 * Render the drone engine to a stereo WAV.
 *
 *   build/render_drone --sec 30 --out drone.wav
 *   build/render_drone --preset bell --sec 60 --out bell.wav
 *
 * Presets are starting points for listening, not the instrument's UI. The
 * firmware will drive the same engine from MIDI CC (docs/MIDI_SPEC.md).
 */
#include "../engine/fm1_drone.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR    44100u
#define BLOCK 256u

struct VoiceSetup {
    uint8_t note, shape, timbre, color, detune;
    uint8_t drift_rate, drift_depth;
    uint8_t comb_tune, comb_fb, comb_mix;
    uint8_t level, pan;
};

struct Preset {
    const char *name;
    const char *about;
    uint8_t     spread;
    VoiceSetup  v[FM1_VOICES];
};

/* note shape timbre color detune | drate ddepth | ctune cfb cmix | lvl pan
 *
 * Revised after listening. The notes were: drift was one-dimensional ("a very
 * detuned multi saw"), breath was "a wall of white noise with a high-pitched
 * screech", bell's attack was "grizzled" (that one was the comb gain bug, now
 * fixed in fm1_comb.h), and glass had the most convincing movement. */
static const Preset kPresets[] = {
/* Was four saw-family voices, so it had one timbre and nothing to listen
 * across. Now each voice is a different family -- saw stack, swarm, additive,
 * ringing sine -- and the comb depth rises with register so the top voice
 * sings rather than thickening the same texture. */
{ "drift", "detuned stacks under an additive top, voices in contrast", 24, {
    { 29, braids::MACRO_OSC_SHAPE_TRIPLE_SAW,   70, 64, 64,  14, 46, 64,  40, 30, 104,  26 },
    { 36, braids::MACRO_OSC_SHAPE_SAW_SWARM,    84, 70, 68,  10, 58, 64,  52, 40,  88, 100 },
    { 43, braids::MACRO_OSC_SHAPE_HARMONICS,    46, 80, 60,  22, 66, 52,  74, 56,  72,  46 },
    { 55, braids::MACRO_OSC_SHAPE_TRIPLE_SINE,  40, 64, 66,   8, 38, 40,  96, 72,  58,  86 }}},

/* The grizzle was the comb clipping internally, not the voicing, so the
 * structure stands. Feedback raised now that it resonates instead of
 * limiting. */
{ "bell",  "struck bells over a bowed bed, long comb tails",    12, {
    { 29, braids::MACRO_OSC_SHAPE_BOWED,        60, 50, 64,   6, 35, 64,  40, 22,  96,  64 },
    { 53, braids::MACRO_OSC_SHAPE_STRUCK_BELL,  80, 70, 64,  12, 20, 52, 110, 92,  76,  30 },
    { 60, braids::MACRO_OSC_SHAPE_STRUCK_BELL,  64, 90, 68,  16, 28, 40, 106, 88,  66, 100 },
    { 41, braids::MACRO_OSC_SHAPE_PLUCKED,      70, 60, 60,  22, 44, 64,  98, 80,  64,  78 }}},

/* The noise was carrying the preset instead of colouring it, and a high-Q
 * comb on noise is where the screech came from. Noise dropped 54 -> 22 with
 * almost no comb, vowel moved down an octave and detuned off the flute, and
 * the two wind voices now lead. */
{ "breath","flute and reed, noise as colour not as bed",        30, {
    { 34, braids::MACRO_OSC_SHAPE_FLUTED,       72, 60, 64,  18, 50, 64,  46, 40, 104,  36 },
    { 41, braids::MACRO_OSC_SHAPE_BLOWN,        56, 50, 58,  14, 44, 64,  34, 30,  92,  96 },
    { 34, braids::MACRO_OSC_SHAPE_VOWEL_FOF,    52, 64, 72,  24, 58, 58,  28, 24,  64,  70 },
    { 29, braids::MACRO_OSC_SHAPE_FILTERED_NOISE,34,86, 64,  30, 70, 72,  10,  8,  22,  52 }}},

/* The one that worked, so only more of what made it work: deeper drift. */
{ "glass", "harmonics and wavetables, slow spectral morph",     18, {
    { 36, braids::MACRO_OSC_SHAPE_HARMONICS,    30, 64, 64,   9, 78, 64,  44, 30,  96,  52 },
    { 48, braids::MACRO_OSC_SHAPE_WAVETABLES,   50, 72, 70,  13, 74, 64,  52, 40,  80,  76 },
    { 43, braids::MACRO_OSC_SHAPE_HARMONICS,    90, 40, 58,   7, 62, 52,  60, 46,  72,  28 },
    { 55, braids::MACRO_OSC_SHAPE_SAW_COMB,     64, 88, 66,  17, 54, 64,  76, 62,  56, 100 }}},
};
static const int kPresetCount = (int)(sizeof kPresets / sizeof *kPresets);

static void apply(fm1::Drone &d, const Preset &p)
{
    d.SetGlobal(G_SPREAD, p.spread);
    for (uint8_t i = 0; i < FM1_VOICES; i++) {
        const VoiceSetup &s = p.v[i];
        d.SetParam(i, P_SHAPE,       s.shape);
        d.SetParam(i, P_TIMBRE,      s.timbre);
        d.SetParam(i, P_COLOR,       s.color);
        d.SetParam(i, P_DETUNE,      s.detune);
        d.SetParam(i, P_DRIFT_RATE,  s.drift_rate);
        /* P_DRIFT_DEPTH split into two attenuverters when drift gained a pitch
         * path. The presets' depth becomes P_FM -- drift into pitch is the one
         * that beats against the comb and the other voices, which is what
         * these presets were voiced around. P_MOD stays at its closed default:
         * on the TRIPLE_* and wavetable shapes used here TIMBRE is a quantised
         * table, and modulating it steps rather than glides. */
        d.SetParam(i, P_FM,          s.drift_depth);

        /* The comb is a filter mode now. F1 in COMB mode with these three
         * parameters is exactly the old osc -> comb -> level -> pan chain, so
         * the presets are unchanged in sound. */
        /* The comb is an insert slot now, not a filter mode. */
        d.SetParam(i, P_SL(0, P_SL_TYPE), SL_COMB);
        d.SetParam(i, P_F1_CUT,      s.comb_tune);
        d.SetParam(i, P_SL(0, P_SL_P2),   s.comb_fb);
        d.SetParam(i, P_SL(0, P_SL_MIX),  s.comb_mix);
        d.SetParam(i, P_LEVEL,       s.level);
        d.SetParam(i, P_PAN,         s.pan);
        d.NoteOn(i, s.note);
    }
}

int main(int argc, char **argv)
{
    const char *preset = "drift", *out = "drone.wav";
    int secs = 20, restrike_ms = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int last = (i + 1 >= argc);
        if (!strcmp(a, "--list")) {
            for (int p = 0; p < kPresetCount; p++)
                printf("%-8s %s\n", kPresets[p].name, kPresets[p].about);
            return 0;
        }
        else if (!strcmp(a, "--preset")   && !last) preset = argv[++i];
        else if (!strcmp(a, "--sec")      && !last) secs = atoi(argv[++i]);
        else if (!strcmp(a, "--restrike") && !last) restrike_ms = atoi(argv[++i]);
        else if (!strcmp(a, "--out")      && !last) out = argv[++i];
        else {
            fprintf(stderr, "usage: render_drone [--preset NAME] [--sec S]\n"
                            "                    [--restrike MS] [--out FILE] [--list]\n");
            return 2;
        }
    }

    const Preset *sel = 0;
    for (int p = 0; p < kPresetCount; p++)
        if (!strcmp(kPresets[p].name, preset)) { sel = &kPresets[p]; break; }
    if (!sel) { fprintf(stderr, "render_drone: unknown preset '%s' (--list)\n", preset); return 2; }

    uint32_t frames = SR * (uint32_t)secs;
    int16_t *buf = (int16_t *)calloc((size_t)frames * 2u, sizeof *buf);
    if (!buf) { fprintf(stderr, "render_drone: out of memory\n"); return 1; }

    fm1::Drone drone;
    drone.Init();
    apply(drone, *sel);

    uint32_t rs = restrike_ms > 0 ? (uint32_t)restrike_ms * SR / 1000u : 0u;
    uint32_t next = rs;
    int strikes = 0;

    for (uint32_t i = 0; i < frames; i += BLOCK) {
        uint32_t n = (frames - i < BLOCK) ? frames - i : BLOCK;
        if (rs && i >= next) {
            /* re-excite the impulse-driven shapes; harmless for the rest */
            for (uint8_t v = 0; v < FM1_VOICES; v++) drone.Strike(v);
            next += rs; strikes++;
        }
        drone.Render(buf + (size_t)i * 2u, n);
    }

    int rc = wav_write(out, buf, frames, SR, 2);
    if (rc == 0) {
        printf("%s: preset '%s', %u frames @ %u Hz stereo (%d s)",
               out, sel->name, frames, SR, secs);
        if (strikes) printf(", %d restrikes", strikes);
        printf("\n  %s\n", sel->about);
    } else {
        fprintf(stderr, "render_drone: could not write %s\n", out);
    }
    free(buf);
    return rc ? 1 : 0;
}
