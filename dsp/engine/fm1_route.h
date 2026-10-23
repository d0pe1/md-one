/* SPDX-License-Identifier: MIT
 *
 * What effects exist, and where a signal can go.
 *
 * Split out of fm1_rack.h for the same reason fm1_fmode.h was split out of
 * fm1_filter.h: that header embeds braids::Svf and is therefore C++ only,
 * while the firmware's UI is C and has to put an effect name and a
 * destination on the screen. These are parameter VALUES, so they belong
 * beside the P_* and G_* indices in fm1_params.h, which are also plain C.
 *
 * fm1_rack.h includes this, so nothing that used the enums had to change.
 */
#ifndef FM1_ROUTE_H
#define FM1_ROUTE_H

/* Deliberately no reverb and no delay. The output goes to a DAW over USB
 * audio where both are free and better; the RAM buys tuned resonance and a
 * patchbay instead. */
enum {
    FX_NONE = 0,
    FX_CHORUS,      /* modulated short delay, stereo by construction */
    FX_FOLD,        /* wavefolder: drive, then reflect */
    FX_WIDTH,       /* mid/side width */
    FX_LP,          /* state-variable low pass, stereo */
    FX_HP,
    FX_NTYPES
};

static const char *const FM1_FXNAME[FX_NTYPES] = {
    "OFF", "CHORUS", "FOLD", "WIDTH", "LOWPASS", "HIGHPASS"
};

/* Where a node's output goes. One value, one cable. Shared by voices and
 * effect channels -- a voice routes exactly the way an effect does, which is
 * what makes the mental model one thing instead of two.
 *
 * CH1..4 are the MIXER CHANNELS: each sums whatever points at it, applies a
 * level and a pan, and lands in the SUM. The sum is the last stage and goes
 * to the output; nothing routes out of it.
 *
 *     voices / FX  ->  CH1..4  ->  SUM  ->  OUT
 *
 * A channel has no destination of its own, deliberately -- one that could be
 * routed into an effect would make the graph cyclic in a way nothing else
 * here is, and the case it would serve (submix, then reverb) is already
 * reachable by pointing those voices at the same effect channel.
 *
 * OUT is the sum reached directly, for a voice that wants no channel strip
 * between it and the output. */
enum {
    DEST_OFF = 0,   /* unplugged: this node is not heard, nor is anything
                     * whose only path to the output runs through it */
    DEST_FX1, DEST_FX2, DEST_FX3, DEST_FX4,
    DEST_MIX1, DEST_MIX2, DEST_MIX3, DEST_MIX4,
    DEST_OUT,
    DEST_NDEST
};

static const char *const FM1_DESTNAME[DEST_NDEST] = {
    "---", "FX1", "FX2", "FX3", "FX4",
    "CH1", "CH2", "CH3", "CH4", "SUM"
};

#define FM1_FX_CHANS   4
#define FM1_MIX_CHANS  4

/* Asked by predicate, never by arithmetic on the enum.
 *
 * The rack used to test `d >= DEST_FX1 && d < DEST_OUT` to mean "an effect
 * channel", which was true only while the effects were the last thing before
 * OUT. Adding the mix buses between them would have made that test quietly
 * include a bus and index fx[] past its end. */
#define DEST_IS_FX(d)   ((d) >= DEST_FX1  && (d) < DEST_FX1  + FM1_FX_CHANS)
#define DEST_IS_MIX(d)  ((d) >= DEST_MIX1 && (d) < DEST_MIX1 + FM1_MIX_CHANS)
#define DEST_FX_IDX(d)  ((uint32_t)((d) - DEST_FX1))
#define DEST_MIX_IDX(d) ((uint32_t)((d) - DEST_MIX1))

#endif /* FM1_ROUTE_H */
