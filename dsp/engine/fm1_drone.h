/* SPDX-License-Identifier: MIT
 *
 * The drone engine: four always-on voices, each a Braids macro oscillator
 * through a tuned comb, with slow independent drift, mixed to stereo.
 *
 * Shaped by what the measurements said (docs/BRAIDS_PORT.md):
 *
 *   - four voices is 12.9% of the audio budget at the most expensive shape,
 *     and 68.6 KiB of the 390 KiB of RAM available, so neither is a limit;
 *   - every voice keeps the full MacroOscillator, so any of the 48 shapes can
 *     play on any voice;
 *   - no reverb: the output goes to a DAW over USB audio where space is free
 *     and better. The pool buys tuned combs instead.
 *
 * Everything here is integer: the chip has no FPU and a single float fails the
 * link. dsp/target/check.sh enforces that.
 */
#ifndef FM1_DRONE_H
#define FM1_DRONE_H

#include "../mi/fm1_braids.h"
#include "../shared/fm1_fixed.h"
#include "fm1_drift.h"
#include "fm1_env.h"
#include "fm1_eq.h"
#include "fm1_filter.h"
#include "fm1_params.h"
#include "fm1_rack.h"

#define FM1_VOICES 4
#define FM1_BLOCK  256      /* the firmware's audio ISR block */

namespace fm1 {

#define FM1_FILTERS 2       /* per voice: OSC -> F1 -> VCA -> F2 -> pan */

struct DroneVoice {
    BraidsVoice  osc;
    fm1_filter_t f[FM1_FILTERS];
    fm1_eq_t     eq;            /* three multimode bands, after F2 */
    int32_t      drive_q12;     /* Q12_ONE = clean, bypasses the saturator */
    fm1_drift_t  drift;
    fm1_env_t    env;           /* the VCA; `gate` below is its input */

    uint8_t      p[NPARAM];     /* current parameter values, 0..max */
    int16_t      note;          /* MIDI note, 0..127 */
    int16_t      pitch_base;    /* note+octave+detune+spread, before drift */
    uint8_t      gate;          /* sounding at all */

    int16_t      scratch[FM1_BLOCK];
};

class Drone {
 public:
    void Init(uint32_t seed = 0x51F0A7u);

    /* --- control, from the main loop ----------------------------------- */

    /* MIDI note on: sets pitch, strikes, opens the gate. 14 of Braids' shapes
     * are silent without the strike. */
    void NoteOn(uint8_t voice, uint8_t note);
    void NoteOff(uint8_t voice);

    /* Silence or restore a voice WITHOUT re-striking it. NoteOn re-excites the
     * shape, which is wrong for mute/solo: un-soloing a bowed voice should
     * return it as it was, not re-bow it. */
    void SetGate(uint8_t voice, uint8_t on);

    /* Re-excite without changing pitch -- a re-bow rather than a new note. */
    void Strike(uint8_t voice);

    /* Set a per-voice parameter (index from fm1_params.h, value 0..max). */
    void SetParam(uint8_t voice, uint8_t param, uint8_t value);
    void SetGlobal(uint8_t param, uint8_t value);

    uint8_t GetParam(uint8_t voice, uint8_t param) const;

    /* The UI has to SHOW a global, not only set one. Without this the FX and
     * GLO pages could edit values they could not display. */
    uint8_t GetGlobal(uint8_t param) const;

    /* Post-fader peak for the mixer meter, 0..127, with a slow fall.
     *
     * Post-fader because the meter has one job -- showing what the channel
     * actually contributes -- and a channel pulled down must read quiet or it
     * is lying about the balance. Taken after F2 and before pan, so panning a
     * voice moves it without changing what its meter says.
     *
     * Peak rather than RMS, falling about 130 dB/s: a drone that sits
     * steadily hot and one that peaks once look identical on a fast meter,
     * and on this instrument nearly everything is the first kind. */
    uint8_t Meter(uint8_t voice) const
    { return voice < FM1_VOICES ? meter_[voice] : 0; }

    /* Did this channel reach full scale recently? The meter turns entirely
     * red when it did.
     *
     * Held for ~0.5 s rather than reported instantaneously: a clip lasting
     * one block is 5.8 ms, which no eye catches, and an overload you cannot
     * see is the same as one that did not happen. */
    bool Clipping(uint8_t voice) const
    { return voice < FM1_VOICES && clip_[voice] != 0; }

    /* --- audio, from the ISR -------------------------------------------- */

    /* n stereo frames, interleaved L R L R. ADDS to out, as app_audio() does,
     * so the buffer must be zeroed or carry something to mix into. */
    void Render(int16_t *out, uint32_t n);

 private:
    void ApplyPitch(uint8_t v);

    DroneVoice v_[FM1_VOICES];
    uint8_t    g_[NGPARAM];
    uint8_t    meter_[FM1_VOICES];      /* see Meter() */
    uint8_t    clip_[FM1_VOICES];       /* blocks of red remaining */
    uint32_t   blocks_;        /* blocks rendered, for drift scheduling */

    /* The mix bus. int32 so voices accumulate with headroom and saturation
     * happens ONCE, after master -- not on every voice add, which distorted
     * the mix rather than limiting it. 2 KiB; a member rather than a local
     * because the user stack is only ~24 KiB. */
    int32_t    bus_[FM1_BLOCK * 2];

    /* The FX patchbay. Shared, so its cost is paid once rather than per
     * voice, and routed per node rather than ordered. */
    fx_rack_t  rack_;

    /* What the voices routed to each effect channel this block. Separate from
     * bus_ because bus_ is the master output -- a voice set to DEST_OUT goes
     * straight there and never touches these. 8 KiB. */
    int32_t    fxin_[FM1_FX_CHANS][FM1_BLOCK * 2];
};

}  // namespace fm1

#endif /* FM1_DRONE_H */
