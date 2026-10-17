/* SPDX-License-Identifier: MIT
 *
 * A C interface to the drone engine.
 *
 * The firmware application is C (Dinghy's platform is a C unity build); the
 * engine is C++ because Braids is. This is the seam between them -- the only
 * place the two meet, so neither has to accommodate the other.
 *
 * Audio note: Dinghy's app_audio() hands out int32_t stereo frames and expects
 * the app to ADD to them (docs/API.md). The engine renders int16_t, so the
 * render entry point here does the widening add.
 */
#ifndef FM1_DRONE_C_H
#define FM1_DRONE_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors of the parameter indices, so the C app does not need the C++ header.
 * fm1_drone_c.cc static_asserts these against the real enum. */
#define FD_P_SHAPE        0
#define FD_P_TIMBRE       1
#define FD_P_COLOR        2
#define FD_P_FM           3     /* drift -> pitch,  bipolar, 64 = none */
#define FD_P_MOD          4     /* drift -> timbre, bipolar, 64 = none */
#define FD_P_DRIFT_RATE   5
#define FD_P_OCTAVE       6
#define FD_P_DETUNE       7
#define FD_P_GLIDE        8
/* Two multimode filters, four parameters each. These replaced the three
 * CMBTUNE/CMBFB/CMBMIX parameters: the comb is now a filter MODE, not a
 * separate block (dsp/engine/fm1_filter.h). */
#define FD_P_F1_TYPE      9
#define FD_P_F1_CUT      10
#define FD_P_F1_RES      11
#define FD_P_F1_MIX      12
#define FD_P_F2_TYPE     13
#define FD_P_F2_CUT      14
#define FD_P_F2_RES      15
#define FD_P_F2_MIX      16
#define FD_P_LEVEL       17
#define FD_P_PAN         18
#define FD_P_MONO        19     /* collapse to centre, ignoring PAN */
#define FD_P_DEST        20     /* FD_DEST_* -- this voice's one cable out */
#define FD_NPARAM        21

/* Filter modes, mirroring the F_* enum in fm1_filter.h. */
#define FD_F_OFF          0
#define FD_F_LP           1
#define FD_F_BP           2
#define FD_F_HP           3
#define FD_F_COMB         4
#define FD_F_RESO         5
#define FD_F_NMODES       6

#define FD_G_MASTER       0
#define FD_G_SPREAD       1
#define FD_G_BINAURAL     2
#define FD_G_HAAS         3
#define FD_G_DRIFT_SYNC   4

/* The FX patchbay: four effect channels, five controls each (TYPE, P1, P2,
 * MIX, DEST). FD_G_FX(n, j) addresses channel n, control j. */
#define FD_G_FX_BASE      5
#define FD_G_FX_STRIDE    5
#define FD_G_FX(n, j)     (FD_G_FX_BASE + (n) * FD_G_FX_STRIDE + (j))
#define FD_G_FX_TYPE      0
#define FD_G_FX_P1        1
#define FD_G_FX_P2        2
#define FD_G_FX_MIX       3
#define FD_G_FX_DEST      4
#define FD_NGPARAM       25

/* Routing targets, mirroring the DEST_* enum in fm1_rack.h. One value, one
 * cable. Used by both FD_P_DEST and FD_G_FX(n, FD_G_FX_DEST). */
#define FD_DEST_OFF       0
#define FD_DEST_FX1       1
#define FD_DEST_FX2       2
#define FD_DEST_FX3       3
#define FD_DEST_FX4       4
#define FD_DEST_OUT       5
#define FD_DEST_NDEST     6

/* Effect types, mirroring the FX_* enum in fm1_rack.h. */
#define FD_FX_NONE        0
#define FD_FX_CHORUS      1
#define FD_FX_FOLD        2
#define FD_FX_WIDTH       3
#define FD_FX_LP          4
#define FD_FX_HP          5
#define FD_FX_NTYPES      6

#define FD_VOICES         4

void fm1_drone_init(void);

void fm1_drone_note_on(uint8_t voice, uint8_t note);
void fm1_drone_note_off(uint8_t voice);
void fm1_drone_strike(uint8_t voice);

/* Mute/unmute without re-striking -- what mute and solo need. */
void fm1_drone_set_gate(uint8_t voice, uint8_t on);

void    fm1_drone_set_param(uint8_t voice, uint8_t param, uint8_t value);
uint8_t fm1_drone_get_param(uint8_t voice, uint8_t param);
void    fm1_drone_set_global(uint8_t param, uint8_t value);

/* Post-fader peak for voice v, 0..127, with a slow fall. For the mixer
 * meters. */
uint8_t fm1_drone_meter(uint8_t voice);

/* The label for a parameter, for the screen. Never NULL. */
const char *fm1_drone_param_name(uint8_t param);

/* MIDI control change -> parameter. channel is 0-based: 0..3 are the voices,
 * 15 is global. Returns 1 if the message was consumed. */
int fm1_drone_midi_cc(uint8_t channel, uint8_t cc, uint8_t value);

/* Add n stereo frames to out (L R L R ...), as app_audio() expects. */
void fm1_drone_render_add(int32_t *out, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* FM1_DRONE_C_H */
