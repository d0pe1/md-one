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
/* Channel EQ: three multimode bands, five controls each (MODE, FREQ, GAIN,
 * RES, MIX). FD_P_EQ(b, j) addresses band b (0..2), control j. */
#define FD_P_EQ_BASE     17
#define FD_P_EQ_STRIDE    5
#define FD_P_EQ(b, j)    (FD_P_EQ_BASE + (b) * FD_P_EQ_STRIDE + (j))
#define FD_P_EQ_MODE      0
#define FD_P_EQ_FRQ       1
#define FD_P_EQ_GAIN      2
#define FD_P_EQ_RES       3
#define FD_P_EQ_MIX       4
#define FD_EQ_BANDS       3

/* Band modes. 0 is the additive shape, 1 the subtractive one; which it means
 * depends on the band -- see FM1_EQ_MODE_NAME in fm1_eq.h. */
#define FD_EQB_SHELF      0     /* low and high bands */
#define FD_EQB_PASS       1     /* low -> highpass, high -> lowpass */
#define FD_EQB_PEAK       0     /* mid band */
#define FD_EQB_NOTCH      1

#define FD_P_DRIVE       32
/* VCA envelope, AHDSR. Every time segment spans ~2 ms .. ~32 s; SUSTAIN is
 * a level, not a time. */
#define FD_P_ATTACK      33
#define FD_P_HOLD        34
#define FD_P_DECAY       35
#define FD_P_SUSTAIN     36
#define FD_P_RELEASE     37
#define FD_P_LEVEL       38
#define FD_P_PAN         39
#define FD_P_MONO        40     /* collapse to centre, ignoring PAN */
#define FD_P_DEST        41     /* FD_DEST_* -- this voice's one cable out */

/* The modulation matrix: nine destinations, each a DEPTH (bipolar, 64 = none)
 * and a SOURCE (FD_MD_SRC_NONE, or 1..4 for the LFOs).
 * FD_P_MD(n, j) addresses destination n, control j. */
#define FD_P_MD_BASE     42
#define FD_P_MD_STRIDE    2
#define FD_P_MD(n, j)    (FD_P_MD_BASE + (n) * FD_P_MD_STRIDE + (j))
#define FD_P_MD_DEPTH     0
#define FD_P_MD_SRC       1
#define FD_MD_SRC_NONE    0

/* What each destination reaches, in the order the MOD pages walk them. */
#define FD_MD_PITCH       0
#define FD_MD_TIMBRE      1
#define FD_MD_COLOR       2
#define FD_MD_LEVEL       3
#define FD_MD_F1CUT       4
#define FD_MD_F1RES       5
#define FD_MD_F2CUT       6
#define FD_MD_F2RES       7
#define FD_MD_PAN         8
#define FD_MD_NDEST       9

#define FD_NPARAM        60

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

/* Four LFOs, four controls each. FD_G_LFO(n, j) addresses LFO n, control j. */
#define FD_G_LFO_BASE    25
#define FD_G_LFO_STRIDE   4
#define FD_G_LFO(n, j)   (FD_G_LFO_BASE + (n) * FD_G_LFO_STRIDE + (j))
#define FD_G_LFO_RATE     0
#define FD_G_LFO_AMT      1
#define FD_G_LFO_PHASE    2
#define FD_G_LFO_MODE     3
#define FD_LFOS           4
#define FD_NGPARAM       41

/* LFO shapes, mirroring the LFO_* enum in fm1_lfo.h. */
#define FD_LFO_TRI        0
#define FD_LFO_SAW        1
#define FD_LFO_RAMP       2
#define FD_LFO_SQR        3
#define FD_LFO_SH         4
#define FD_LFO_SMOOTH     5
#define FD_LFO_NMODES     6

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
/* The UI has to SHOW a global, not only set one -- without this the FX and
 * GLO pages could edit values they could not display. */
uint8_t fm1_drone_get_global(uint8_t param);

/* Continuous parameters are slewed toward their target over ~45 ms, so a
 * 7-bit step does not click. Call this after setting MANY at once -- loading
 * a patch is not a knob turn, and hearing 42 parameters slide into place is
 * a glitch rather than a feature. */
void fm1_drone_snap_params(void);

/* Which envelope segment voice v is in: FD_ENV_*. The ENV page marks it on
 * the curve -- on a drone machine an envelope is often minutes long, so
 * "where am I" is a real question. */
uint8_t fm1_drone_env_stage(uint8_t voice);

/* LFO n's output right now, -32768..32767, already scaled by AMOUNT. The LFO
 * page draws it moving: a modulator you cannot see is one you have to guess
 * at, and at 0.1 Hz guessing takes ten seconds a go. */
int16_t fm1_drone_lfo_value(uint8_t n);

/* Mirrors of the ENV_* enum in fm1_env.h, in the order the curve draws. */
#define FD_ENV_IDLE     0
#define FD_ENV_ATTACK   1
#define FD_ENV_HOLD     2
#define FD_ENV_DECAY    3
#define FD_ENV_SUSTAIN  4
#define FD_ENV_RELEASE  5
#define FD_ENV_STAGES   6

/* Post-fader peak for voice v, 0..127, with a slow fall. For the mixer
 * meters. */
uint8_t fm1_drone_meter(uint8_t voice);

/* Non-zero while voice v has recently reached full scale -- the meter is
 * drawn entirely red. Held about half a second. */
uint8_t fm1_drone_clipping(uint8_t voice);

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
