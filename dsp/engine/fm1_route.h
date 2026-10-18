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
 * what makes the mental model one thing instead of two. */
enum {
    DEST_OFF = 0,   /* unplugged: this node is not heard, nor is anything
                     * whose only path to the output runs through it */
    DEST_FX1, DEST_FX2, DEST_FX3, DEST_FX4,
    DEST_OUT,
    DEST_NDEST
};

static const char *const FM1_DESTNAME[DEST_NDEST] = {
    "---", "FX1", "FX2", "FX3", "FX4", "OUT"
};

#define FM1_FX_CHANS   4

#endif /* FM1_ROUTE_H */
