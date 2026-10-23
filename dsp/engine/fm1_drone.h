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
#include "fm1_slot.h"
#include "fm1_params.h"
#include "fm1_lfo.h"
#include "fm1_rack.h"

#define FM1_VOICES 4
#define FM1_BLOCK  256      /* the firmware's audio ISR block */

namespace fm1 {

#define FM1_FILTERS 2       /* per voice: OSC -> F1 -> VCA -> F2 -> pan */

struct DroneVoice {
    BraidsVoice  osc;
    fm1_filter_t f[FM1_FILTERS];
    /* Two insert slots, between the filters. This is where COMB and RESO
     * went when they stopped being filter modes -- stackable now, and each
     * carries the delay line the filter gave up. */
    fm1_slot_t   sl[FM1_SLOTS];
    fm1_eq_t     eq;            /* three multimode bands, after F2 */
    int32_t      drive_q12;     /* Q12_ONE = clean, bypasses the saturator */
    fm1_drift_t  drift;
    fm1_env_t    env;           /* the VCA; `gate` below is its input */

    /* Three arrays, because a slewed parameter has three different answers
     * to "what is it".
     *
     *   p    what the RENDERER reads -- the value in force this block. Every
     *        read site in Render() already used this name, so slewing changed
     *        none of them.
     *   tgt  where it is heading: what was last set by a knob, a CC or a
     *        patch. GetParam returns this, so the screen shows what you asked
     *        for rather than a number crawling toward it.
     *   acc  the same value in Q8. Needed because p is a byte: a slew of one
     *        step per block would stall forever without somewhere to keep the
     *        fraction.
     *
     * tgt IS Q8 TOO, which is what makes fine editing possible at all.
     *
     * Braids takes its two parameters as 0..32767, and this engine has always
     * carried that precision in `acc` -- then thrown it away, because tgt was
     * a byte and every block truncated back to one. A knob could name 128
     * destinations while an LFO reaching the SAME destination ran at full
     * width: one parameter, two resolutions, depending on what moved it.
     *
     * These are ENCODERS, not pots -- unlimited travel -- so 128 was never a
     * physical limit, only a storage one. With tgt in Q8 a coarse click is
     * +-256 and a fine click is +-1, and the existing slew carries both
     * without a line of it changing.
     *
     * p stays a byte deliberately. It is what the CC map, the patch format
     * and the screen all speak, and widening it would cost all three to buy
     * resolution an enum or a filter-table index cannot use. */
    uint8_t      p[NPARAM];
    int16_t      tgt[NPARAM];   /* Q8, as acc */
    int16_t      acc[NPARAM];   /* Q8; 127 << 8 = 32512, inside int16 */
    int16_t      note;          /* MIDI note, 0..127 */
    int16_t      pitch_base;    /* note+octave+detune+spread, before drift */
    /* Where the pitch actually IS, which during a glide is behind
     * pitch_base. Separate because the comb tunes from the base and must not
     * slide with the portamento -- a resonator chasing the note is a
     * different instrument from one the note slides against. */
    int32_t      pitch_now;
    int32_t      drift_now;     /* this block's drift, Q15, for the matrix */
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

    /* Nudge a parameter by `delta` SIXTEENTH-BIT steps -- the fine gesture.
     *
     * 256 of these make one coarse step, so one click here is 1/32768 of the
     * range rather than 1/128. It is the only way to reach the resolution
     * Braids actually takes: VOWEL blends across a 12-bit field and gets 16
     * of its 4096 values from a coarse knob.
     *
     * `snap` is the coarse gesture instead: it rounds to the 128-grid IN THE
     * DIRECTION OF TRAVEL first, so a coarse click after a fine edit always
     * both moves and lands on the grid -- which is what keeps exact centre
     * reachable, and on a bipolar control centre is what OFF means.
     *
     * Returns the new coarse value, for the screen. */
    uint8_t NudgeParam(uint8_t voice, uint8_t param, int32_t delta, int snap);
    uint8_t NudgeGlobal(uint8_t param, int32_t delta, int snap);

    /* Does this parameter carry more than its coarse byte to the engine?
     * Only where the extra bits reach something -- offering precision that
     * changes nothing is the same lie as a mislabelled knob. */
    static int HasFine(uint8_t param);

    /* The fractional part of a target, 0..255, and a way to put it back.
     * The patch saves these: without them a fine edit survives until the
     * next power cycle and then quietly vanishes, which is the same silent
     * loss the patch magic exists to prevent. */
    uint8_t GetFine(uint8_t voice, uint8_t param) const;
    void    SetFine(uint8_t voice, uint8_t param, uint8_t frac);

    uint8_t GetParam(uint8_t voice, uint8_t param) const;

    /* Pitch in 1/128 semitone to Hz in Q16, interpolated within the semitone.
     *
     * What the comb and the resonators tune from. Public and static because
     * it used to quantise to whole semitones, which made the slots jump an
     * entire semitone the moment detune went one cent flat -- audible as the
     * harmonics changing instrument at the centre detent, and invisible to
     * every test that only looked at the pitch. */
    static q16_t PitchToHz(int32_t pitch);

    /* The voice's pitch before drift, in 1/128 semitone -- note, octave,
     * detune and spread, exactly as it reaches the oscillator.
     *
     * Exposed so tuning can be TESTED rather than listened for. The detune
     * knob was asymmetric about its own centre for a while, by one pitch
     * unit, which is inaudible as a pitch and unmistakable as a step when you
     * cross zero. Nothing could see it: the parameter read back correctly,
     * the sound was the only evidence, and the sound is not a unit test. */
    int16_t PitchBase(uint8_t voice) const
    { return voice < FM1_VOICES ? v_[voice].pitch_base : 0; }

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

    /* The mix buses have their own meters: a bus is where several voices land,
     * so a voice meter cannot tell you whether the SUM is too hot. */
    uint8_t MixMeter(uint8_t ch) const
    { return ch < FM1_MIX_CHANS ? mixmeter_[ch] : 0; }

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

    /* An LFO's output right now, Q15 bipolar and already scaled by AMOUNT.
     * The LFO page draws it moving: a modulator you cannot see is one you
     * have to guess at, and at 0.1 Hz guessing takes ten seconds a go. */
    q15_t LfoValue(uint8_t n) const
    { return n < FM1_LFOS ? lfo_out_[n] : 0; }

    /* Which envelope segment a voice is in, ENV_* from fm1_env.h. The ENV
     * page marks it on the curve: on a drone machine the envelope is often
     * minutes long, so "where am I" is a real question rather than a
     * decoration. */
    uint8_t EnvStage(uint8_t voice) const
    { return voice < FM1_VOICES ? v_[voice].env.stage : (uint8_t)ENV_IDLE; }

    /* The value actually IN FORCE this block, which during a slew is behind
     * the target. Not what the screen should show -- that is GetParam -- but
     * a test cannot otherwise see whether a slew arrived, and "it stalled one
     * step short" is exactly the failure worth catching. */
    uint8_t CurrentParam(uint8_t voice, uint8_t param) const
    { return (voice < FM1_VOICES && param < NPARAM) ? v_[voice].p[param] : 0; }

    /* Land every slewed parameter on its target at once.
     *
     * A patch load is not a knob turn: restoring 42 parameters and then
     * hearing them slide into place over 45 ms is a glitch, not a feature.
     * Call this after setting many parameters at once. */
    void SnapParams();

 private:
    void ApplyPitch(uint8_t v);

    /* Put `value` into force for real: store it in p[] and tell whichever
     * block owns it. Split out of SetParam so the slew can reach the same
     * code -- two copies of that switch would drift apart by the second
     * parameter anyone added. */
    void ApplyParam(uint8_t voice, uint8_t param, uint8_t value);
    void ApplyGlobal(uint8_t param, uint8_t value);

    /* One slew step for everything in flight. Called once per block. */
    void SlewBlock();

    /* What the matrix is delivering to destination n of this voice, Q15
     * bipolar: the chosen LFO's output scaled by the depth knob, and zero
     * when either is at rest. Each caller scales it into its own units,
     * because a full-depth swing means +-12 semitones on pitch and +-64
     * steps on a level, and nothing sensible can average the two. */
    int32_t ModOf(const DroneVoice &d, uint8_t n) const;

    /* A 0..127 parameter with its modulation already folded in and clamped.
     * ApplyPitch and the per-block filter update must agree on what a
     * modulated cutoff IS, or whichever runs last silently wins. */
    uint8_t EffParam(const DroneVoice &d, uint8_t param, uint8_t dest) const;
    int32_t EffCutP(const DroneVoice &d, uint8_t param, uint8_t dest) const;

    DroneVoice v_[FM1_VOICES];
    uint8_t    g_[NGPARAM];         /* in force now, as DroneVoice::p */
    int16_t    gtgt_[NGPARAM];  /* Q8, as gacc_ */
    int16_t    gacc_[NGPARAM];

    /* Four shared modulators, stepped once per block. Shared is the point:
     * the per-voice wander is already what fm1_drift.h does, and these give
     * the opposite -- all four voices moving together. */
    fm1_lfo_t  lfo_[FM1_LFOS];
    q15_t      lfo_out_[FM1_LFOS];  /* this block's value, for the screen */

    /* HAAS: a few milliseconds of delay on one channel. Below the echo
     * threshold the ear fuses the two and localises toward the earlier one,
     * so the image widens without the tone changing -- no filter, no EQ.
     *
     * 1536 samples is 34.8 ms at 44.1 kHz, past the 30 ms where it starts
     * being heard as a distinct repeat rather than as width. 3 KiB. */
    int16_t    haas_[1536];
    uint32_t   haas_w_;
    int8_t     haas_side_;      /* -1 left, +1 right, 0 off */
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

    /* The four MIX BUSES. Whatever points at one of these -- voices or effect
     * channels alike -- sums here; the bus then applies its own level and pan
     * and goes to bus_. No destination of its own, so no cycle is possible
     * through a bus and none of the rack's feedback analysis applies to them.
     * 8 KiB. */
    /* One voice's stereo output, before it is handed to its cables.
     *
     * The voice used to write straight into its single destination from
     * inside the sample loop. With four cables that would mean up to four
     * scattered writes per sample; rendering once here and distributing the
     * BLOCK afterwards costs one buffer and turns the fan-out into the same
     * cheap add-loop the patchbay uses. 2 KiB, reused by every voice. */
    int32_t    vout_[FM1_BLOCK * 2];

    int32_t    mixin_[FM1_MIX_CHANS][FM1_BLOCK * 2];
    uint8_t    mixmeter_[FM1_MIX_CHANS];
};

}  // namespace fm1

#endif /* FM1_DRONE_H */
