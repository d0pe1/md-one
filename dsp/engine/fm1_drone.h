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
    fm1_drift_t  drift;

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

    /* --- audio, from the ISR -------------------------------------------- */

    /* n stereo frames, interleaved L R L R. ADDS to out, as app_audio() does,
     * so the buffer must be zeroed or carry something to mix into. */
    void Render(int16_t *out, uint32_t n);

 private:
    void ApplyPitch(uint8_t v);

    DroneVoice v_[FM1_VOICES];
    uint8_t    g_[NGPARAM];
    uint32_t   blocks_;        /* blocks rendered, for drift scheduling */

    /* The mix bus. int32 so voices accumulate with headroom and saturation
     * happens ONCE, after master -- not on every voice add, which distorted
     * the mix rather than limiting it. 2 KiB; a member rather than a local
     * because the user stack is only ~24 KiB. */
    int32_t    bus_[FM1_BLOCK * 2];

    /* The global FX rack runs on that bus, after the voices sum and before
     * master. Shared, so its cost is paid once rather than per voice. */
    fx_rack_t  rack_;
};

}  // namespace fm1

#endif /* FM1_DRONE_H */
