/* SPDX-License-Identifier: MIT
 *
 * Names and categories for Braids' 48 oscillator shapes.
 *
 * Braids itself puts these on a four-character LED display and gets away with
 * it, because its users learn the codes over months. We have a 240x240 screen
 * and no such excuse, so the names are spelled out and the shapes are grouped
 * -- 48 on a bare encoder is thirty detents between a saw stack and a bowed
 * string, which is unusable. SHIFT + ALGORITHM jumps whole categories and
 * turns that into three.
 *
 * The order is braids::MacroOscillatorShape in dsp/mi/braids/settings.h and
 * must stay in step with it: the index IS the parameter value. That header is
 * C++ and namespaced, so the firmware's C UI cannot read it -- hence this.
 * A test checks the count; nothing can check the spelling, so these were
 * transcribed against the enum rather than from memory.
 */
#ifndef FM1_SHAPES_H
#define FM1_SHAPES_H

#include <stdint.h>

#define FM1_NSHAPES 48

static const char *const FM1_SHAPE_NAME[FM1_NSHAPES] = {
    /*  0 */ "CSAW",            "MORPH",           "SAW SQUARE",
    /*  3 */ "SINE TRIANGLE",   "BUZZ",
    /*  5 */ "SQUARE SUB",      "SAW SUB",         "SQUARE SYNC",
    /*  8 */ "SAW SYNC",
    /*  9 */ "TRIPLE SAW",      "TRIPLE SQUARE",   "TRIPLE TRIANGLE",
    /* 12 */ "TRIPLE SINE",     "TRIPLE RING MOD",
    /* 14 */ "SAW SWARM",       "SAW COMB",        "TOY",
    /* 17 */ "DIGITAL FILT LP", "DIGITAL FILT PK", "DIGITAL FILT BP",
    /* 20 */ "DIGITAL FILT HP",
    /* 21 */ "VOSIM",           "VOWEL",           "VOWEL FOF",
    /* 24 */ "HARMONICS",
    /* 25 */ "FM",              "FEEDBACK FM",     "CHAOTIC FM",
    /* 28 */ "PLUCKED",         "BOWED",           "BLOWN",
    /* 31 */ "FLUTED",
    /* 32 */ "STRUCK BELL",     "STRUCK DRUM",     "KICK",
    /* 35 */ "CYMBAL",          "SNARE",
    /* 37 */ "WAVETABLES",      "WAVE MAP",        "WAVE LINE",
    /* 40 */ "WAVE PARAPHONIC",
    /* 41 */ "FILTERED NOISE",  "TWIN PEAKS",      "CLOCKED NOISE",
    /* 44 */ "GRANULAR CLOUD",  "PARTICLE NOISE",
    /* 46 */ "DIGITAL MOD",     "QUESTION MARK",
};

/* --- categories ----------------------------------------------------------
 *
 * Twelve groups, each a contiguous run; docs/UX_DESIGN.md section 5 lists
 * them. `first` is the index SHIFT + ALGORITHM lands on.
 *
 * The drum shapes are near-useless for drones but are NOT hidden. They live
 * in one category you skip in a single jump, which costs nothing and avoids a
 * "where did it go" problem later.
 */
#define FM1_NSHAPE_CATS 12

typedef struct {
    const char *name;
    uint8_t     first;          /* first shape index in this category */
} fm1_shape_cat_t;

static const fm1_shape_cat_t FM1_SHAPE_CAT[FM1_NSHAPE_CATS] = {
    { "ANALOG",    0  },
    { "SUB SYNC",  5  },
    { "TRIPLE",    9  },
    { "SWARM",     14 },
    { "FILTER",    17 },
    { "VOWEL",     21 },
    { "HARMONIC",  24 },
    { "PHYSICAL",  28 },
    { "STRUCK",    32 },
    { "WAVETABLE", 37 },
    { "NOISE",     41 },
    { "MISC",      46 },
};

/* Which category a shape belongs to. Linear over twelve entries, called once
 * per redraw -- a lookup table would be 48 bytes to save nothing. */
static inline uint8_t fm1_shape_cat(uint8_t shape)
{
    uint8_t c = FM1_NSHAPE_CATS - 1;
    while (c > 0 && shape < FM1_SHAPE_CAT[c].first) c--;
    return c;
}

/* --- quantised shapes ----------------------------------------------------
 *
 * On these, TIMBRE and COLOR index a TABLE rather than sweeping a range:
 * semitone intervals on the TRIPLE shapes, frequency ratios on the FM ones,
 * discrete waves on the wavetable ones. The knob steps instead of gliding.
 *
 * The screen needs to know, because a knob that jumps reads as a knob that is
 * broken -- it has already caused exactly that confusion once.
 */
static inline int fm1_shape_quantised(uint8_t shape)
{
    return (shape >= 9  && shape <= 13) ||      /* TRIPLE    */
           (shape >= 24 && shape <= 27) ||      /* HARMONIC  */
           (shape >= 37 && shape <= 40);        /* WAVETABLE */
}

/* On the TRIPLE shapes the voice is a CHORD GENERATOR: TIMBRE and COLOR set
 * the intervals of the second and third oscillators. Invisible from the
 * panel, so the screen says so. */
static inline int fm1_shape_is_chord(uint8_t shape)
{
    return shape >= 9 && shape <= 13;
}

#endif /* FM1_SHAPES_H */
