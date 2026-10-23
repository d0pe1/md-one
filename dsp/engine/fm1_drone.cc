/* SPDX-License-Identifier: MIT */
#include "fm1_drone.h"
#include "../shared/fm1_pan_table.h"

namespace fm1 {

/* fm1_params.h writes the filter mode range as a literal so it does not have
 * to include fm1_filter.h (which would drag braids::Svf in and make it C++
 * only). This is where the two are held together.
 *
 * It compares against the literal rather than FM1_PDEF[...].max because that
 * table is `static const`, not constexpr, and C++ will not read it in a
 * constant expression. Adding a mode without widening the parameter's max
 * still fails here, which is the mistake worth catching. */
static_assert(F_NMODES - 1 == 4,
              "filter mode count changed -- update P_F1_TYPE/P_F2_TYPE max in fm1_params.h");
static_assert(SL_NTYPES - 1 == 4,
              "slot type count changed -- update P_SL*_TYPE max in fm1_params.h");
static_assert(F_OFF == 0,
              "F_OFF moved -- update P_F1_TYPE/P_F2_TYPE def in fm1_params.h");
static_assert(FX_NTYPES - 1 == 5,
              "effect count changed -- update G_FX*_TYPE max in fm1_params.h");
static_assert(DEST_NDEST - 1 == 9,
              "destination count changed -- update P_DEST and G_FX*_DEST max");
/* The two blocks have to stay contiguous and in this order, because the
 * engine indexes both by subtraction (DEST_FX_IDX, DEST_MIX_IDX). OUT must
 * stay last so it reads as the end of the list on screen. */
static_assert(DEST_FX1  + FM1_FX_CHANS  == DEST_MIX1,
              "the effect channels and the mix buses are no longer adjacent");
static_assert(DEST_MIX1 + FM1_MIX_CHANS == DEST_OUT,
              "DEST_OUT is no longer the tail of the destination list");
/* The LFO block now sits after the patchbay, so the patchbay is no longer the
 * tail. What still has to hold is that each block is CONTIGUOUS and that
 * ApplyGlobal's arithmetic dispatch can tell them apart: it tests
 * `>= G_LFO_BASE` before `>= G_FX_BASE`, so the order matters. */
static_assert(G_FX(3, G_FX_DEST) + 1 == G_LFO_BASE,
              "the patchbay and LFO parameter blocks are no longer adjacent");
static_assert(G_LFO(3, G_LFO_MODE) + 1 == G_MIX_BASE,
              "the LFO and mix bus parameter blocks are no longer adjacent");
static_assert(G_MIX(FM1_MIX_CHANS - 1, G_MIX_PAN) == NGPARAM - 1,
              "the mix bus parameters are not the tail of the global table");
static_assert(LFO_NMODES - 1 == 5,
              "LFO mode count changed -- update G_LFO*_MODE max in fm1_params.h");

/* Detune and drift are applied in 1/128-semitone units, Braids' pitch unit.
 * DETUNE is 0..100 CENTS around a centre of 50, one cent per click. */
static const int32_t kDetuneSpan = 64;      /* 50 cents = 64 units of 1/128 */

/* One cent, in pitch units of 1/128 semitone, as Q7: 128/100 = 1.28.
 * Braids' pitch resolution IS 1/128 semitone, so 0.78 cents is the finest
 * step that exists -- a 1-cent click lands on the nearest one, which is at
 * worst 0.22 cents out and inaudible. */
static const int32_t kCentInPitch = 164;

/* Pitch (1/128 semitone) to Hz in Q16, INTERPOLATED WITHIN THE SEMITONE.
 *
 * This is what the comb and the resonators tune from, and until now it threw
 * the fraction away:
 *
 *     int32_t semis = (pitch / kPitchPerSemitone) - 69;
 *
 * An integer divide, so the comb only ever landed on whole semitones. At a
 * note exactly on the grid, pitch is N*128 and semis is N. One cent FLAT
 * makes pitch N*128 - 1, which divides to N - 1 -- so the first click below
 * centre dropped the comb A WHOLE SEMITONE, and it stayed there for the
 * entire negative half (-50 cents is only -64 units, never reaching the next
 * boundary). The positive half never crossed one at all.
 *
 * That is the "completely different harmonics from -1 cent onward" reported
 * from the panel: a resonator a semitone off against its own oscillator is a
 * different instrument, and it switched between them at the centre detent.
 * The earlier rounding fix made the PITCH symmetric and could not touch this,
 * because the fault was downstream in what the pitch was then used for.
 *
 * Linear interpolation between adjacent semitone ratios is plenty: one
 * semitone spans 5.9%, over which 2^x is nearly straight, so the error stays
 * under about 0.2 cents -- far below the 0.78 cent resolution of the pitch
 * unit feeding it.
 *
 * Exposed as a static so the continuity can be tested rather than heard.
 */
q16_t Drone::PitchToHz(int32_t pitch)
{
    /* 2^(n/12) in Q12, with the octave as a thirteenth entry so the top
     * semitone interpolates toward 2.0 instead of wrapping to 1.0. */
    static const uint16_t kSemi[13] = {
        4096, 4339, 4596, 4868, 5157, 5462,
        5786, 6129, 6492, 6877, 7286, 7719, 8192
    };
    /* Floor, deliberately: the shift must round down on both sides so that
     * `frac` is always the positive distance above `semis`. This is the one
     * place an arithmetic shift does exactly the right thing. */
    int32_t total = pitch - 69 * kPitchPerSemitone;
    int32_t semis = total >> 7;
    int32_t frac  = total & 127;
    int32_t oct = semis / 12, rem = semis % 12;
    uint32_t a, b, m, f;

    if (rem < 0) { rem += 12; oct -= 1; }
    a = kSemi[rem];
    b = kSemi[rem + 1];
    m = a + (((b - a) * (uint32_t)frac) >> 7);              /* Q12 */
    f = (uint32_t)((440u << 16) >> 12) * m;                 /* Q16 */
    if (oct >= 0) f <<= (uint32_t)oct; else f >>= (uint32_t)(-oct);
    return (q16_t)f;
}

/* Cents to pitch units, ROUNDED SYMMETRICALLY ABOUT ZERO.
 *
 * `(cents * 164) >> 7` looks like a divide by 128 and is not: an arithmetic
 * right shift rounds toward NEGATIVE INFINITY, so it truncates above zero and
 * floors below it. The two sides of the knob then behave differently:
 *
 *      +1 cent ->  +164 >> 7 =  1.28 ->  +1 unit
 *      -1 cent ->  -164 >> 7 = -1.28 ->  -2 units
 *
 * The first click below centre moved twice as far as the first click above,
 * which is audible as a step exactly at the zero crossing and nowhere else --
 * reported from the panel as "0 => -1 cent has a very distinct jump ... not
 * from 0 => +1 ... and -1 => -50 is also without a noticeable jump". Further
 * out every step is the same ~1.28, so only the crossing gives it away.
 *
 * Rounding half away from zero instead makes -n the exact mirror of +n, which
 * is what a bipolar control has to be: its centre detent is a real zero, and
 * either side of it must cost the same. */
static inline int32_t fm1_cents_to_pitch(int32_t cents)
{
    int32_t n = cents * kCentInPitch;
    return n >= 0 ? (n + 64) >> 7 : -((-n + 64) >> 7);
}

/* ~0.5 s of red at 256 frames per block, 44.1 kHz. */
static const uint8_t kClipHoldBlocks = 86;

void Drone::Init(uint32_t seed)
{
    for (uint8_t i = 0; i < NPARAM; i++) {
        /* defaults from the table, so the engine and the UI agree.
         * In force, targeted and settled all at once -- booting is not a
         * knob turn, and an instrument that slid into tune over its first
         * 45 ms would be making a noise nobody asked for. */
        for (uint8_t v = 0; v < FM1_VOICES; v++) {
            v_[v].p[i]   = FM1_PDEF[i].def;
            v_[v].tgt[i] = (int16_t)(FM1_PDEF[i].def << 8);
            v_[v].acc[i] = (int16_t)(FM1_PDEF[i].def << 8);
        }
    }
    for (uint8_t i = 0; i < NGPARAM; i++) {
        g_[i]    = FM1_GDEF[i].def;
        gtgt_[i] = (int16_t)(FM1_GDEF[i].def << 8);
        gacc_[i] = (int16_t)(FM1_GDEF[i].def << 8);
    }
    fx_rack_init(&rack_);

    /* A different seed each, or four S&H modes would step in lockstep and
     * read as one modulator wired four ways. */
    for (uint8_t n = 0; n < FM1_LFOS; n++) {
        fm1_lfo_init(&lfo_[n], 0x2545F491u + n * 0x9E3779B9u);
        lfo_out_[n] = 0;
    }

    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        DroneVoice &d = v_[v];
        d.osc.Init();
        for (uint8_t fi = 0; fi < FM1_FILTERS; fi++) fm1_filter_init(&d.f[fi]);
        for (uint8_t si = 0; si < FM1_SLOTS; si++)    fm1_slot_init(&d.sl[si]);
        fm1_eq_init(&d.eq);
        fm1_env_init(&d.env);
        d.drive_q12 = Q12_ONE;
        /* a different seed per voice, or all four drift in lockstep */
        fm1_drift_init(&d.drift, seed + v * 0x9E3779B9u);
        d.note = 36 + 12 * v;          /* spread across octaves by default */
        d.gate = 0;
        d.osc.set_shape((braids::MacroOscillatorShape)d.p[P_SHAPE]);
        ApplyPitch(v);
    }
    for (uint32_t i = 0; i < 1536u; i++) haas_[i] = 0;
    haas_w_ = 0;
    haas_side_ = 0;
    blocks_ = 0;
}

void Drone::ApplyPitch(uint8_t v)
{
    DroneVoice &d = v_[v];

    int32_t pitch = (int32_t)d.note * kPitchPerSemitone;

    /* OCTAVE is 0..8 with 4 as centre */
    pitch += ((int32_t)d.p[P_OCTAVE] - 4) * 12 * kPitchPerSemitone;

    /* DETUNE, in CENTS, one per click, bipolar around 50.
     *
     * The pitch unit is 1/128 of a semitone, so a cent is 128/100 of one --
     * kCentInPitch is that ratio in Q7. The previous version added the raw
     * knob offset as if a click were a pitch unit, which made the real range
     * +-50 cents while the screen said +-64: the knob was honest about
     * neither its span nor its step. */
    pitch += fm1_cents_to_pitch((int32_t)d.p[P_DETUNE] - 50);

    /* SPREAD fans the voices apart, voice 0 lowest */
    int32_t spread = (int32_t)g_[G_SPREAD];
    if (spread) {
        /* Doubled, so the fan is SYMMETRIC about the note.
         *
         * `(v - (FM1_VOICES-1)/2)` is integer arithmetic: (4-1)/2 is 1, so
         * the multipliers were -1, 0, +1, +2 where four voices need
         * -1.5, -0.5, +0.5, +1.5. Opening SPREAD therefore raised the
         * ensemble's pitch centre instead of fanning around it -- +25 cents
         * at the top of the knob -- and voice 1 never moved at all.
         *
         * Working in halves keeps it exact without a fraction. */
        int32_t off = ((int32_t)(2 * v) - (FM1_VOICES - 1))
                    * spread * kDetuneSpan / (2 * 127);
        pitch += off;
    }

    if (pitch < 0) pitch = 0;
    if (pitch > 127 * kPitchPerSemitone) pitch = 127 * kPitchPerSemitone;

    /* Stored, not applied: Render() adds the per-block drift on top. */
    d.pitch_base = (int16_t)pitch;
    d.osc.set_pitch((int16_t)pitch);

    /* The comb tracks the BASE pitch and does not follow the drift. A fixed
     * resonator against a drifting source is what produces the slow beating;
     * if the comb drifted too they would move together and the beats would
     * disappear. It also keeps two divisions out of the per-block path.
     *
     * fm1_phase_inc gives the increment the comb needs to derive its delay
     * without a 64-bit divide. */
    q16_t hz_q16 = 0;
    {
        hz_q16 = PitchToHz(pitch);
    }
    uint32_t inc = fm1_phase_inc(hz_q16, FM1_K_44100);
    /* The filters only need the key while KEYTRACK is up. The SLOTS need it
     * always: a comb tunes from the phase increment and a resonator tracks
     * the note, and both of those moved out of the filter. */
    fm1_filter_set_key(&d.f[0], (int16_t)pitch);
    fm1_filter_set_key(&d.f[1], (int16_t)pitch);
    for (uint8_t si = 0; si < FM1_SLOTS; si++)
        fm1_slot_set_key(&d.sl[si], (int16_t)pitch, inc);
}

void Drone::NoteOn(uint8_t voice, uint8_t note)
{
    if (voice >= FM1_VOICES) return;
    v_[voice].note = (int16_t)note;
    v_[voice].gate = 1;
    /* A voice that has never sounded has nowhere to glide FROM, and sliding
     * up from note 0 on the first keypress is not portamento, it is a
     * swoop nobody asked for. */
    if (!v_[voice].pitch_now) v_[voice].pitch_now = v_[voice].pitch_base;
    fm1_env_gate(&v_[voice].env, 1);
    ApplyPitch(voice);
    v_[voice].osc.Strike();
}

void Drone::NoteOff(uint8_t voice)
{
    if (voice >= FM1_VOICES) return;
    v_[voice].gate = 0;
    fm1_env_gate(&v_[voice].env, 0);
}

void Drone::SetGate(uint8_t voice, uint8_t on)
{
    if (voice >= FM1_VOICES) return;
    {
        uint8_t g = on ? 1 : 0;
        /* ONLY ON AN EDGE.
         *
         * apply_gates() in the firmware loops all four voices and calls this
         * for each on every key press, every MUTE and every SOLO -- so an
         * unconditional gate restarted the ATTACK of the three voices you did
         * not touch, every time you touched one.
         *
         * It is silent at the shipped default because SUSTAIN is 127: the
         * level is already at the top, so the step is zero and the stage
         * machine falls straight through. Lower SUSTAIN -- the most ordinary
         * thing anyone does with an AHDSR -- and the whole bed swells and
         * decays on every keypress. With a long attack that is a thirty-second
         * swell on voices nobody asked to retrigger, and no way for the player
         * to avoid it except to leave SUSTAIN at 127, which disables HOLD and
         * DECAY as well.
         *
         * Re-gating to the state it already holds is not a gesture. */
        if (v_[voice].gate == g) return;
        v_[voice].gate = g;
        fm1_env_gate(&v_[voice].env, g);
    }
}

void Drone::Strike(uint8_t voice)
{
    if (voice < FM1_VOICES) v_[voice].osc.Strike();
}

void Drone::SetParam(uint8_t voice, uint8_t param, uint8_t value)
{
    if (voice >= FM1_VOICES || param >= NPARAM) return;
    if (value > FM1_PDEF[param].max) value = FM1_PDEF[param].max;

    DroneVoice &d = v_[voice];
    if (d.tgt[param] == (int16_t)(value << 8)) return;
    d.tgt[param] = (int16_t)(value << 8);

    /* A slewed parameter only gets a new destination here; SlewBlock walks
     * it there. Everything else lands now -- an enum has nowhere to slide
     * to, and waiting a block to change filter mode would just be lag. */
    if (!fm1_param_is_smoothed(param)) {
        d.acc[param] = (int16_t)(value << 8);
        ApplyParam(voice, param, value);
    }
}

void Drone::ApplyParam(uint8_t voice, uint8_t param, uint8_t value)
{
    DroneVoice &d = v_[voice];
    /* A fine-capable parameter must NOT be skipped when its coarse byte is
     * unchanged -- that is the whole state a fine edit lives in. Everything
     * else still short-circuits, which is what keeps this cheap on the
     * hundred-odd parameters that have no fraction. */
    if (d.p[param] == value && !HasFine(param)) return;
    d.p[param] = value;

    switch (param) {
    case P_SHAPE:
        d.osc.set_shape((braids::MacroOscillatorShape)value);
        break;
    case P_OCTAVE: case P_DETUNE:
        ApplyPitch(voice);
        break;

    /* CUT is just a cutoff now. It used to drive the comb's delay too, which
     * is why it went through ApplyPitch; the comb left with the resonators.
     *
     * From the ACCUMULATOR rather than the byte, in 1/128 semitone: acc is
     * Q8 of a 0..127 knob and the filter's unit is half that. Taking `value`
     * here would round the cutoff back to a whole semitone every block and
     * quietly undo both the fine edit and the smooth modulation. */
    case P_F1_CUT:
        fm1_filter_set_cut_p(&d.f[0], (int32_t)d.acc[P_F1_CUT] >> 1);
        break;
    case P_F2_CUT:
        fm1_filter_set_cut_p(&d.f[1], (int32_t)d.acc[P_F2_CUT] >> 1);
        break;

    case P_F1_TYPE: fm1_filter_set_mode(&d.f[0], value); break;
    case P_F2_TYPE: fm1_filter_set_mode(&d.f[1], value); break;

    case P_F1_POLE: fm1_filter_set_poles(&d.f[0], value); break;
    case P_F2_POLE: fm1_filter_set_poles(&d.f[1], value); break;

    case P_F1_RES:  fm1_filter_set_res(&d.f[0], value); break;
    case P_F2_RES:  fm1_filter_set_res(&d.f[1], value); break;
    case P_F1_MIX:  fm1_filter_set_mix(&d.f[0], value); break;
    case P_F2_MIX:  fm1_filter_set_mix(&d.f[1], value); break;

    case P_DRIVE:   d.drive_q12 = value ? fm1_drive_gain(value) : Q12_ONE; break;
    case P_ATTACK:  fm1_env_set_attack(&d.env, value);  break;
    case P_HOLD:    fm1_env_set_hold(&d.env, value);    break;
    case P_DECAY:   fm1_env_set_decay(&d.env, value);   break;
    case P_SUSTAIN: fm1_env_set_sustain(&d.env, value); break;
    case P_RELEASE: fm1_env_set_release(&d.env, value); break;
    case P_ENV_MODE: fm1_env_set_mode(&d.env, value);   break;

    default:
        /* The insert slots, dispatched arithmetically like the EQ below. A
         * type change needs the key reapplied: a comb that has just become a
         * comb has no delay yet. */
        if (param >= P_SL_BASE && param < P_SL_BASE + FM1_SLOTS * P_SL_STRIDE) {
            uint8_t n = (uint8_t)((param - P_SL_BASE) / P_SL_STRIDE);
            fm1_slot_t *sl = &d.sl[n];
            switch ((param - P_SL_BASE) % P_SL_STRIDE) {
            case P_SL_TYPE: fm1_slot_set_type(sl, value); ApplyPitch(voice); break;
            case P_SL_P1:   fm1_slot_set_p1(sl, value);   ApplyPitch(voice); break;
            case P_SL_P2:   fm1_slot_set_p2(sl, value);   break;
            case P_SL_MIX:  fm1_slot_set_mix(sl, value);  break;
            default: break;
            }
            break;
        }
        /* The EQ is a regular block of five-per-band, like the patchbay's
         * channels, so it is dispatched arithmetically rather than with
         * fifteen case labels. */
        if (param >= P_EQ_BASE && param < P_EQ_BASE + EQ_BANDS * P_EQ_STRIDE) {
            uint8_t band = (uint8_t)((param - P_EQ_BASE) / P_EQ_STRIDE);
            switch ((param - P_EQ_BASE) % P_EQ_STRIDE) {
            case P_EQ_MODE: fm1_eq_set_mode(&d.eq, band, value); break;
            case P_EQ_FRQ:  fm1_eq_set_freq(&d.eq, band, value); break;
            case P_EQ_GAIN: fm1_eq_set_gain(&d.eq, band, value); break;
            case P_EQ_RES:  fm1_eq_set_res(&d.eq, band, value);  break;
            case P_EQ_MIX:  fm1_eq_set_mix(&d.eq, band, value);  break;
            default: break;
            }
        }
        break;      /* anything else is read directly at render time */
    }
}

void Drone::SetGlobal(uint8_t param, uint8_t value)
{
    if (param >= NGPARAM) return;
    if (value > FM1_GDEF[param].max) value = FM1_GDEF[param].max;
    if (gtgt_[param] == (int16_t)(value << 8)) return;
    gtgt_[param] = (int16_t)(value << 8);

    if (!fm1_gparam_is_smoothed(param)) {
        gacc_[param] = (int16_t)(value << 8);
        ApplyGlobal(param, value);
    }
}

void Drone::ApplyGlobal(uint8_t param, uint8_t value)
{
    if (g_[param] == value) return;
    g_[param] = value;
    if (param == G_SPREAD)
        for (uint8_t v = 0; v < FM1_VOICES; v++) ApplyPitch(v);

    /* Tested before the patchbay, because the LFO block sits above it and
     * `>= G_FX_BASE` is true for both. */
    if (param >= G_LFO_BASE) {
        uint8_t n    = (uint8_t)((param - G_LFO_BASE) / G_LFO_STRIDE);
        uint8_t what = (uint8_t)((param - G_LFO_BASE) % G_LFO_STRIDE);
        fm1_lfo_t *l = &lfo_[n];
        switch (what) {
        case G_LFO_RATE:  fm1_lfo_set_rate(l, value);   break;
        case G_LFO_AMT:   fm1_lfo_set_amount(l, value); break;
        case G_LFO_PHASE: fm1_lfo_set_phase(l, value);  break;
        case G_LFO_MODE:  fm1_lfo_set_mode(l, value);   break;
        default: break;
        }
        return;
    }

    if (param >= G_FX_BASE) {
        uint8_t slot = (uint8_t)((param - G_FX_BASE) / G_FX_STRIDE);
        uint8_t what = (uint8_t)((param - G_FX_BASE) % G_FX_STRIDE);
        fx_chan_t *c = &rack_.ch[slot];
        switch (what) {
        case G_FX_TYPE: fx_chan_set_type(c, value); break;
        case G_FX_P1:   fx_chan_set_p1(c, value);   break;
        case G_FX_P2:   fx_chan_set_p2(c, value);   break;
        case G_FX_MIX:  c->mix = value;             break;
        case G_FX_DEST: fx_chan_set_dest(c, value); break;
        default: break;
        }
    }
}

/* The TARGET, not the value in force. What the screen shows and what a patch
 * saves is what you asked for; a knob that read back a number crawling toward
 * where you put it would look broken, and a patch saved mid-slew would store
 * a value nobody chose. */
uint8_t Drone::GetParam(uint8_t voice, uint8_t param) const
{
    if (voice >= FM1_VOICES || param >= NPARAM) return 0;
    return (uint8_t)(v_[voice].tgt[param] >> 8);
}

uint8_t Drone::GetGlobal(uint8_t param) const
{
    if (param >= NGPARAM) return 0;
    return (uint8_t)(gtgt_[param] >> 8);
}

/* Which parameters carry their fine bits all the way to something audible.
 *
 * Only TIMBRE and COLOR today, and that is not a placeholder -- it is where
 * the resolution exists to be had. Braids takes those two as 0..32767, and
 * several shapes read far down the word: VOWEL picks its vowel from the top
 * four bits and BLENDS across the low twelve, so a coarse knob reaches 16 of
 * its 4096 blend positions.
 *
 * The filters deliberately do NOT. fm1_svf_f[] is indexed by MIDI note
 * 0..127, so seven bits is exactly the table's own resolution and a fine
 * click would move nothing until that table is interpolated. Offering
 * precision that changes nothing is the same lie as a mislabelled knob, one
 * step quieter -- so it is not offered. */
int Drone::HasFine(uint8_t param)
{
    /* The filter cutoffs joined once the filter stopped rounding them. They
     * were excluded on the grounds that fm1_svf_f[] is indexed by MIDI note
     * and seven bits IS its resolution -- true of the table, false of the
     * filter: braids::Svf interpolates, and the one-pole does now too. The
     * conclusion was wrong because it looked at the table instead of at what
     * reads it. */
    /* The envelope times joined because seven bits is genuinely coarse over
     * a range this wide: fm1_env_a[] sweeps 0.4 ms to 32 s in 128 steps, so
     * one click near the top moves a swell by seconds. SUSTAIN is a level
     * rather than a time and gets no finer treatment; ENV MODE is an enum
     * and has nowhere to slide to. */
    return param == P_TIMBRE || param == P_COLOR ||
           param == P_F1_CUT || param == P_F2_CUT ||
           param == P_ATTACK || param == P_HOLD ||
           param == P_DECAY  || param == P_RELEASE;
}

/* The shared body of both nudges: walk a Q8 target by `delta`, either in
 * fine steps or snapped to the coarse grid. */
static int32_t NudgeQ8(int32_t cur, int32_t delta, int snap, uint8_t max)
{
    int32_t out;

    if (!snap) {
        out = cur + delta;                      /* fine: one step is 1/256 */
    } else if (cur & 255) {
        /* Off the grid after a fine edit. Round IN THE DIRECTION OF TRAVEL
         * and spend one click doing it, so a coarse turn always both moves
         * and lands on the grid -- and exact centre, which is what OFF means
         * on every bipolar control, stays reachable. */
        int32_t base = delta > 0 ? (cur >> 8) + 1 : (cur >> 8);
        int32_t rest = delta > 0 ? delta - 1 : delta + 1;
        out = (base + rest) << 8;
    } else {
        out = ((cur >> 8) + delta) << 8;
    }

    if (out < 0) out = 0;
    if (out > (int32_t)max << 8) out = (int32_t)max << 8;
    return out;
}

uint8_t Drone::NudgeParam(uint8_t voice, uint8_t param, int32_t delta, int snap)
{
    if (voice >= FM1_VOICES || param >= NPARAM) return 0;
    {
        DroneVoice &d = v_[voice];
        int32_t out = NudgeQ8(d.tgt[param], delta, snap, FM1_PDEF[param].max);
        d.tgt[param] = (int16_t)out;
        /* An unslewed parameter has no accumulator walking it anywhere, so
         * it has to land now -- the same split SetParam makes. */
        if (!fm1_param_is_smoothed(param)) {
            d.acc[param] = (int16_t)out;
            ApplyParam(voice, param, (uint8_t)(out >> 8));
        }
        return (uint8_t)(out >> 8);
    }
}

uint8_t Drone::NudgeGlobal(uint8_t param, int32_t delta, int snap)
{
    if (param >= NGPARAM) return 0;
    {
        int32_t out = NudgeQ8(gtgt_[param], delta, snap, FM1_GDEF[param].max);
        gtgt_[param] = (int16_t)out;
        if (!fm1_gparam_is_smoothed(param)) {
            gacc_[param] = (int16_t)out;
            ApplyGlobal(param, (uint8_t)(out >> 8));
        }
        return (uint8_t)(out >> 8);
    }
}

uint8_t Drone::GetFine(uint8_t voice, uint8_t param) const
{
    if (voice >= FM1_VOICES || param >= NPARAM) return 0;
    return (uint8_t)(v_[voice].tgt[param] & 255);
}

void Drone::SetFine(uint8_t voice, uint8_t param, uint8_t frac)
{
    if (voice >= FM1_VOICES || param >= NPARAM) return;
    {
        DroneVoice &d = v_[voice];
        int32_t out = ((int32_t)(d.tgt[param] >> 8) << 8) + frac;
        if (out > (int32_t)FM1_PDEF[param].max << 8)
            out = (int32_t)FM1_PDEF[param].max << 8;
        d.tgt[param] = (int16_t)out;
        if (!fm1_param_is_smoothed(param)) d.acc[param] = (int16_t)out;
    }
}

void Drone::SnapParams()
{
    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        DroneVoice &d = v_[v];
        for (uint8_t i = 0; i < NPARAM; i++) {
            d.acc[i] = d.tgt[i];
            ApplyParam(v, i, (uint8_t)(d.tgt[i] >> 8));
        }
    }
    for (uint8_t i = 0; i < NGPARAM; i++) {
        gacc_[i] = gtgt_[i];
        ApplyGlobal(i, (uint8_t)(gtgt_[i] >> 8));
    }
}

/* One slew step for everything in flight, once per block.
 *
 * The snap is not an optimisation, it is what makes the slew terminate. A
 * one-pole approaches asymptotically: `(t - s) >> 3` rounds to zero once the
 * gap is under eight, so without a snap the last fraction never lands and
 * every slewed parameter sits permanently just short of where it was put --
 * quietly flat, quietly dark, quietly quiet. It would also be invisible from
 * outside, because GetParam returns the target.
 *
 * Snapping a whole step early rather than at the stall point costs nothing
 * audible and saves the 36 blocks of tail where the value is already
 * finished. Same lesson as the envelope (SIGNAL_PATH.md section 8): watch the
 * step, not the distance.
 */
static inline int32_t slew_step(int32_t s, int32_t t)
{
    int32_t gap = t - s;
    if (gap < 0) gap = -gap;
    if (gap < FM1_SLEW_SNAP) return t;
    return s + ((t - s) >> FM1_SLEW_SHIFT);
}

void Drone::SlewBlock()
{
    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        DroneVoice &d = v_[v];
        for (uint8_t i = 0; i < NPARAM; i++) {
            int32_t t = (int32_t)d.tgt[i];
            int32_t s = d.acc[i];

            if (s == t) continue;               /* settled: one compare */
            s = slew_step(s, t);
            d.acc[i] = (int16_t)s;
            ApplyParam(v, i, (uint8_t)(s >> 8));
        }
    }

    for (uint8_t i = 0; i < NGPARAM; i++) {
        int32_t t = (int32_t)gtgt_[i];
        int32_t s = gacc_[i];

        if (s == t) continue;
        s = slew_step(s, t);
        gacc_[i] = (int16_t)s;
        ApplyGlobal(i, (uint8_t)(s >> 8));
    }
}

int32_t Drone::ModOf(const DroneVoice &d, uint8_t n) const
{
    uint8_t src = d.p[P_MD(n, P_MD_SRC)];
    int32_t depth;

    /* Unrouted, or routed with the depth at its centre detent: the same
     * silence reached two ways, and both cost one compare. */
    if (src == MD_SRC_NONE || src >= MD_SRC_COUNT) return 0;
    depth = (int32_t)d.p[P_MD(n, P_MD_DEPTH)] - 64;
    if (depth == 0) return 0;

    /* The voice's own drift, which is why this is not simply an LFO index:
     * every voice has a different one, so routing DRIFT to four voices gives
     * four unrelated wanders where an LFO gives one shared movement. */
    if (src == MD_SRC_DRIFT)
        return (d.drift_now * depth) >> 6;

    /* The voice's own envelope, read where the LFOs are read: once per block.
     * Per-sample would be the obvious thing and is wrong here -- these
     * destinations are block-rate by construction (a filter coefficient, a
     * pan gain), so sampling the envelope faster than the thing it drives
     * would only cost cycles.
     *
     * Q24 down to Q15, then the same >> 6 every other source uses, so depth
     * means the same amount of movement whatever is plugged in. UNIPOLAR:
     * 0..32767 rather than +-32767, because an envelope has no negative half
     * -- a positive depth opens, a negative one closes. */
    if (src == MD_SRC_ENV)
        return ((d.env.level >> (FM1_ENV_BITS - 15)) * depth) >> 6;

    /* lfo_out_ is +-32767 and depth is -64..+63, so >> 6 lands back in Q15.
     * Bipolar depth is what makes an inverted modulation possible without a
     * second control: two voices on one LFO with opposite depths breathe
     * against each other. */
    return ((int32_t)lfo_out_[src - 1] * depth) >> 6;
}

uint8_t Drone::EffParam(const DroneVoice &d, uint8_t param, uint8_t dest) const
{
    int32_t x = (int32_t)d.p[param] + ((ModOf(d, dest) * 64) >> 15);
    if (x < 0)   x = 0;
    if (x > 127) x = 127;
    return (uint8_t)x;
}

/* The same thing in 1/128 SEMITONE, for the one destination where the extra
 * bits reach a filter that can use them.
 *
 * EffParam rounds to a byte, so an LFO swept a cutoff in 128 semitone jumps
 * -- the stepped-filter zipper, and the most obviously cheap artefact an
 * instrument can make. braids::Svf interpolates its frequency and always
 * has; the parameter threw the resolution away before it arrived.
 *
 * Reads `acc`, not `p`, so a fine edit of the cutoff lands here too. */
int32_t Drone::EffCutP(const DroneVoice &d, uint8_t param, uint8_t dest) const
{
    /* acc is Q8 of a 0..127 knob; the filter wants 1/128 semitone, so the
     * knob's own unit halves. The modulation reaches +-64 knob steps, which
     * is +-8192 here. */
    int32_t x = ((int32_t)d.acc[param] >> 1) + ((ModOf(d, dest) * 8192) >> 15);
    if (x < 0)         x = 0;
    if (x > 127 * 128) x = 127 * 128;
    return x;
}

void Drone::Render(int16_t *out, uint32_t n)
{
    while (n) {
        uint32_t chunk = (n > FM1_BLOCK) ? FM1_BLOCK : n;

        /* Walk every parameter one step toward where it was put. Block rate,
         * because a 172 Hz staircase on a gain is inaudible while a 128-step
         * jump is a click -- and because deciding this per sample would cost
         * 256 times as much for nothing. */
        SlewBlock();

        /* The modulators, once a block, before anything reads them. Four of
         * them is a few dozen cycles: they are shared, so unlike almost
         * everything else here the cost is paid once rather than per voice. */
        for (uint8_t n = 0; n < FM1_LFOS; n++)
            lfo_out_[n] = fm1_lfo_block(&lfo_[n]);

        for (uint32_t i = 0; i < chunk * 2u; i++) bus_[i] = 0;

        /* Is anything actually patched in? Asked once per block, because the
         * answer only changes at knob rate.
         *
         * Without this an idle patchbay cost 58,439 cycles a block -- 4.2% of
         * the audio budget to clear 8 KiB of input buffers and walk four
         * unplugged channels 256 times. An instrument that is not using its
         * FX must not pay for them. */
        /* Meters fall here, once per block, so a voice that is gated off or
         * unplugged decays to zero instead of freezing at its last reading --
         * a stuck meter is worse than no meter. The voice loop below raises
         * them again. */
        for (uint8_t v = 0; v < FM1_VOICES; v++) {
            if (meter_[v]) meter_[v]--;
            if (clip_[v])  clip_[v]--;
        }

        uint8_t  fx_act[FM1_FX_CHANS];
        uint32_t fx_n = fx_rack_active(&rack_, fx_act);

        /* A mix bus is live if anything points at it. Asked once per block,
         * because clearing 2 KiB for a bus nobody feeds is exactly the kind
         * of idle cost the patchbay already learned about. */
        uint8_t mix_live[FM1_MIX_CHANS] = { 0, 0, 0, 0 };
        for (uint8_t v = 0; v < FM1_VOICES; v++) {
            uint8_t d = v_[v].p[P_DEST];
            if (DEST_IS_MIX(d)) mix_live[DEST_MIX_IDX(d)] = 1;
        }
        for (uint32_t k = 0; k < fx_n; k++) {
            uint8_t d = rack_.ch[fx_act[k]].dest;
            if (DEST_IS_MIX(d)) mix_live[DEST_MIX_IDX(d)] = 1;
        }
        for (uint32_t m = 0; m < FM1_MIX_CHANS; m++)
            if (mix_live[m])
                for (uint32_t i = 0; i < chunk * 2u; i++) mixin_[m][i] = 0;

        /* Only the plugged channels' input buffers need clearing; the rest
         * are never read. */
        for (uint32_t k = 0; k < fx_n; k++)
            for (uint32_t i = 0; i < chunk * 2u; i++) fxin_[fx_act[k]][i] = 0;

        for (uint8_t vi = 0; vi < FM1_VOICES; vi++) {
            DroneVoice &d = v_[vi];
            /* NOT `if (!gate) continue`. That stopped a voice mid-waveform
             * -- a step straight to zero, which is a click on every mute.
             * A closed voice keeps rendering until its RELEASE has run out,
             * and only then costs nothing. */
            if (fm1_env_idle(&d.env)) continue;

            /* --- block-rate control -------------------------------------
             * Drift, timbre and pitch move once per block. At 5.8 ms that is
             * far finer than anything audible here, and 256x cheaper than
             * doing it per sample. */
            /* One drift source, two attenuverters -- Braids' FM and
             * MODULATION, which on the module scale CV inputs. Run the drift
             * at full depth and scale per destination, so the two knobs are
             * independent and either can be closed. */
            int32_t dr = fm1_drift_block(&d.drift, d.p[P_DRIFT_RATE], 127);
            /* Published before ModOf is called, because the matrix can now
             * route it anywhere. FM and MOD below are unchanged: they are
             * Braids' own attenuverters and keep their hardwired reach. */
            d.drift_now = dr;
            int32_t fm_amt  = (int32_t)d.p[P_FM]  - 64;     /* -64..+63 */
            int32_t mod_amt = (int32_t)d.p[P_MOD] - 64;

            /* Drift moves pitch AND timbre: the tuning wanders and the
             * spectrum breathes, which together read as "alive". Pitch is the
             * one that matters most -- against the fixed comb and the other
             * voices it produces slow beating. */

            /* FM: drift into pitch. +-50 cents at full deflection -- enough
             * to beat against the fixed comb and the other voices, not enough
             * to sound out of tune. */
            /* GLIDE: the sounding pitch slews toward the note rather than
             * jumping to it. Zero is instant, and must be exactly instant --
             * a portamento knob at zero that still smears is a knob that is
             * broken. Snap-on-stall for the same reason the parameter slew
             * needs one: a shift-based one-pole never arrives. */
            {
                uint8_t gl = d.p[P_GLIDE];
                int32_t base = (int32_t)d.pitch_base;
                if (gl == 0) {
                    d.pitch_now = base;
                } else if (d.pitch_now != base) {
                    /* A CONSTANT RATE, so glide time is proportional to the
                     * interval -- which is what portamento means, and what a
                     * one-pole does not do.
                     *
                     * The one-pole here had two faults, both reported as the
                     * knob not doing what it says. It shifted by `1 + gl>>4`,
                     * so a 0..127 control addressed EIGHT speeds and most of
                     * its travel changed nothing. And its step rounded to
                     * zero for small intervals, where snap-on-stall then sent
                     * the pitch straight to the target -- so at the SLOWEST
                     * setting a semitone snapped instantly while an octave
                     * glided. Slower settings made short moves faster.
                     *
                     * Exponential in the knob, interpolated between the
                     * octaves so every value is a distinct speed: ~1 ms for
                     * an octave at the fast end, ~1.8 s at the slow one. */
                    int32_t hi   = 1024 >> (gl >> 4);
                    int32_t rate = hi - ((hi >> 1) * (int32_t)(gl & 15)) / 16;
                    int32_t diff = base - d.pitch_now;
                    if (rate < 1) rate = 1;
                    if (diff > rate)       d.pitch_now += rate;
                    else if (diff < -rate) d.pitch_now -= rate;
                    else                   d.pitch_now = base;
                }
            }

            int32_t dp = (((dr * 64) >> 15) * fm_amt) >> 6;
            int32_t pitch = d.pitch_now + dp;
            /* The matrix reaches pitch at up to +-12 semitones -- a full
             * octave, which is the range that makes a slow ramp into pitch
             * useful rather than merely a vibrato. The drift above is a
             * separate, finer thing (+-50 cents) and they add. */
            pitch += (ModOf(d, MD_PITCH) * (12 * kPitchPerSemitone)) >> 15;
            if (pitch < 0) pitch = 0;
            if (pitch > 127 * kPitchPerSemitone) pitch = 127 * kPitchPerSemitone;
            d.osc.set_pitch((int16_t)pitch);

            /* MOD: drift into timbre.
             *
             * No automatic suppression here, unlike an earlier version. On
             * the TRIPLE_*, FM and wavetable shapes TIMBRE indexes a
             * QUANTISED table -- semitone intervals, frequency ratios,
             * discrete waves -- so modulating it steps rather than glides.
             * That is a legitimate effect (random chord changes on a TRIPLE
             * shape) and the player should choose it, so MOD simply defaults
             * to centre and does nothing until turned up. Silently ignoring
             * a knob is worse than letting it do something surprising. */
            /* From the ACCUMULATOR, not from the byte.
             *
             * acc is the slewed value in Q8 -- the full width Braids takes --
             * and `p` is that same value with its low eight bits thrown away.
             * Reading p here cost every fine bit the engine had already
             * computed, and made a hand on the knob step in units of 256
             * while an LFO on the same destination moved continuously. One
             * parameter, two resolutions, depending on what moved it.
             *
             * This is also what makes the fine gesture audible at all: a
             * fine click moves acc by 1, which p cannot represent. */
            int32_t timbre = (int32_t)d.acc[P_TIMBRE];
            timbre += (((dr >> 2) * mod_amt) >> 6);
            timbre += ModOf(d, MD_TIMBRE) >> 1;     /* +-half the range */
            if (timbre < 0)     timbre = 0;
            if (timbre > 32767) timbre = 32767;

            int32_t colour = (int32_t)d.acc[P_COLOR];
            colour += ModOf(d, MD_COLOR) >> 1;
            if (colour < 0)     colour = 0;
            if (colour > 32767) colour = 32767;

            d.osc.set_parameters((int16_t)timbre, (int16_t)colour);

            /* The four filter destinations are the expensive ones: unlike a
             * level or a pitch, which are read straight out of a variable,
             * a cutoff has to be pushed back through the setter so the
             * coefficient (and, in COMB mode, the delay tuning) is
             * recomputed. So they are gated on actually being routed --
             * ModOf returns 0 after one compare when they are not, and an
             * unmodulated patch pays nothing at all. */
            {
                /* Both filter cutoffs and both slot P1s. These are the
                 * expensive destinations: unlike a level or a pitch, which
                 * are read straight out of a variable, each of these has to
                 * be pushed back through a setter so a coefficient -- or a
                 * comb's delay -- is recomputed. So they are gated on being
                 * routed at all, and ModOf returns after one compare when
                 * they are not. */
                if (ModOf(d, MD_F1CUT))
                    fm1_filter_set_cut_p(&d.f[0], EffCutP(d, P_F1_CUT, MD_F1CUT));
                if (ModOf(d, MD_F2CUT))
                    fm1_filter_set_cut_p(&d.f[1], EffCutP(d, P_F2_CUT, MD_F2CUT));

                {
                    static const uint8_t SDEST[FM1_SLOTS] = { MD_SL1, MD_SL2 };
                    int retune = 0;
                    for (uint8_t k = 0; k < FM1_SLOTS; k++) {
                        if (ModOf(d, SDEST[k]) == 0) continue;
                        fm1_slot_set_p1(&d.sl[k],
                                        EffParam(d, (uint8_t)P_SL(k, P_SL_P1),
                                                 SDEST[k]));
                        retune = 1;
                    }
                    /* A comb tunes from the voice's phase increment, so a
                     * modulated P1 has to go back through ApplyPitch -- once,
                     * however many slots moved. This is the path that makes
                     * "a slow LFO into comb tune" work.
                     *
                     * ApplyPitch also writes the OSCILLATOR's pitch, from the
                     * base only -- no glide, no drift, no matrix -- and it
                     * runs after the modulated write above. So routing any
                     * slot P1 silently froze the oscillator at pitch_base:
                     * the drift stopped, a pitch LFO went inaudible and GLIDE
                     * became instant, all as a side effect of modulating a
                     * comb. Measured: with a slot routed, the per-block zero
                     * crossings of a drifting voice collapsed from 3..15 to
                     * 6..9.
                     *
                     * The comb tracking the base is deliberate (see
                     * ApplyPitch) -- a resonator chasing the drift would
                     * cancel the beating it exists to make. Only the
                     * oscillator write is wrong, so it is put back. */
                    if (retune) {
                        ApplyPitch(vi);
                        d.osc.set_pitch((int16_t)pitch);
                    }
                }
            }

            d.osc.Render(d.scratch, chunk);

            /* --- per-sample: F1, VCA, F2, pan ---------------------------- */
            int32_t lvl = d.p[P_LEVEL];                 /* 0..127 */
            int32_t pan = d.p[P_PAN];                   /* 0..127, 64 centre */

            /* Level and pan take the matrix in their own 0..127 units, then
             * clamp: a tremolo must not wrap to silence at the top of its
             * swing, and an auto-panner must not jump sides at the edge. */
            lvl += (ModOf(d, MD_LEVEL) * 64) >> 15;
            pan += (ModOf(d, MD_PAN)   * 64) >> 15;
            if (lvl < 0)   lvl = 0;
            if (lvl > 127) lvl = 127;
            if (pan < 0)   pan = 0;
            if (pan > 127) pan = 127;

            /* Voice gain is pan and NOTHING else.
             *
             * It used to be divided by FM1_VOICES, which made every voice
             * quieter as soon as another existed -- turning a voice off
             * changed the level of the rest.
             *
             * CONSTANT POWER, via fm1_pan_table.h. This was a linear law,
             * which gave a centred voice 0.5 per channel against 1.0 hard
             * over -- so PAN was also a 6 dB volume control, and the centre,
             * where a drone bed actually sits, was the quietest place on the
             * knob. Measured on a bare CSAW voice at LEVEL 127: -9.1 dBFS
             * centred, -3.7 dBFS hard left.
             *
             * The comment that used to be here said a drone bed "does not
             * need the 3 dB centre lift". It is not a lift. Constant power
             * is the law that keeps the sweep level; linear is the one that
             * costs 3 dB extra at the centre, which is where it was spent.
             *
             * It buys that 3 dB back out of bus headroom, and that is the
             * right direction here twice over: one voice measured far too
             * quiet, and four at full level were already into the soft
             * clipper, so the headroom was notional anyway.
             *
             * No sqrt on this chip, but none is needed -- the law is decided
             * once per voice per block and lives in a 128-entry table.
             *
             * LEVEL is NOT here -- the VCA applies it inside the sample loop,
             * between the two filters, so that F2 hears a voice that has
             * already been faded. Applying it in both places would square it.
             *
             * MONO overrides PAN rather than resetting it, so switching it
             * off returns the voice to exactly where it was in the image. It
             * reads the table at centre instead of branching to its own pair
             * of constants, which is what used to make a mono voice quieter
             * than a centred one by the same 3 dB. */
            int32_t pq = d.p[P_MONO] ? 64 : pan;
            int32_t gl_q12 = fm1_pan_q12[127 - pq];
            int32_t gr_q12 = fm1_pan_q12[pq];

            /* Where this voice's cable goes. DEST_OFF means the voice is
             * rendered and DISCARDED -- it still costs its cycles, which is
             * correct: unplugging a cable does not unplug the oscillator, and
             * re-plugging it must not restart the sound.
             *
             * It used to `continue` here, which did not do that. Skipping the
             * sample loop skipped fm1_env_tick with it, so a disconnected
             * voice's envelope FROZE: a sounding voice kept level > 0 and a
             * gated one stayed in ATTACK, so fm1_env_idle could never become
             * true and the early-out at the top of this loop could never fire
             * again. The oscillator then rendered every block for the rest of
             * the session -- four voices parked at DEST_OFF is roughly a
             * fifth of the audio budget spent on audio that goes nowhere --
             * and the envelope-stage readout stuck on ATK or SUS for good.
             *
             * So the whole chain runs and only the BUS ADD is dropped, which
             * is what the paragraph above always claimed. A disconnected
             * voice now decays, reaches idle, and stops costing anything. */
            uint8_t  dest = d.p[P_DEST];
            /* DEST_OFF is 0 and DEST_FX1 is 1, so the channel arithmetic
             * would index fxin_[-1]. Nothing is ever written through it --
             * the add below is gated -- but forming the address at all is
             * undefined, so it points at the bus and goes unused. */
            int32_t *sink = bus_;
            if      (DEST_IS_FX(dest))  sink = fxin_[DEST_FX_IDX(dest)];
            else if (DEST_IS_MIX(dest)) sink = mixin_[DEST_MIX_IDX(dest)];

            /* Asked once per block, not once per sample. Both of these are
             * flat on most voices most of the time, and testing inside the
             * loop made them cost real cycles for doing nothing. */
            const bool eq_on    = !fm1_eq_idle(&d.eq);
            const bool drive_on = d.drive_q12 > Q12_ONE;

            /* Block-rate, like eq_on and drive_on: whether a slot does
             * anything only changes when a knob moves. */
            int sl_on[FM1_SLOTS];
            for (uint8_t si = 0; si < FM1_SLOTS; si++)
                sl_on[si] = !fm1_slot_idle(&d.sl[si]);

            int32_t pk = 0;                             /* post-fader peak */
            for (uint32_t i = 0; i < chunk; i++) {
                /* OSC -> F1 -> VCA -> F2. The VCA sits BETWEEN the filters by
                 * design (SIGNAL_PATH.md OPEN-2): F2 in a resonant mode rings
                 * on after the voice stops, like a struck string. The cost is
                 * that muting leaves a tail, which is accepted deliberately --
                 * a hard kill is the job of a separate control, not of this
                 * ordering. */
                int32_t s = fm1_filter_tick(&d.f[0], d.scratch[i]);
                /* VCA: the envelope, then LEVEL. Per sample, because an
                 * envelope stepped once per block is a staircase at 172 Hz
                 * -- which is the zipper noise this is meant to avoid, just
                 * at a different frequency. */
                s = (int32_t)(((int64_t)s * fm1_env_tick(&d.env)) >> FM1_ENV_BITS);
                s = (s * lvl) >> 7;

                /* The insert slots, after the VCA so a resonator rings on
                 * when the voice closes -- the same reason F2 is here rather
                 * than before it. Gated on being in use: an OFF slot is not a
                 * branch inside this loop, it is a flag decided once a
                 * block. */
                if (sl_on[0]) s = fm1_slot_tick(&d.sl[0], (q15_t)s);
                if (sl_on[1]) s = fm1_slot_tick(&d.sl[1], (q15_t)s);

                s = fm1_filter_tick(&d.f[1], (q15_t)s);

                /* Tone, then colour. EQ before DRIVE so you choose what gets
                 * saturated -- lift the mid and the drive bites there --
                 * rather than correcting afterwards for what the drive did. */
                if (eq_on)    s = fm1_eq_tick(&d.eq, (q15_t)s);
                if (drive_on) s = fm1_saturate((s * d.drive_q12) >> Q12_BITS);

                int32_t a = s < 0 ? -s : s;
                if (a > pk) pk = a;

                /* No saturation here. The bus has room; limiting once at the
                 * end is the whole point. */
                if (dest != DEST_OFF) {
                    sink[2 * i]     += (s * gl_q12) >> Q12_BITS;
                    sink[2 * i + 1] += (s * gr_q12) >> Q12_BITS;
                }
            }

            /* Raise the meter; the fall already happened above. */
            {
                /* Divide by Q15_ONE, not >>15. A shift needs pk = 32768 to
                 * reach 127, and the voice path saturates at 32767, so the
                 * top segment of the ladder was unreachable: a channel could
                 * be pinned at full scale and still show one segment short. */
                uint8_t m = (uint8_t)(((int32_t)pk * 127) / Q15_ONE);
                if (m > meter_[vi]) meter_[vi] = m;
                /* At or past full scale: this channel has no headroom of its
                 * own left, whether or not the mix bus still does. Held so a
                 * 5.8 ms overload is still visible half a second later. */
                if (pk >= Q15_ONE) clip_[vi] = kClipHoldBlocks;
            }
        }

        /* --- the FX patchbay ----------------------------------------------
         *
         * Per SAMPLE, not per block. Channels are visited in index order, so
         * a cable pointing forward arrives with no delay and one pointing
         * backward arrives on the next sample -- 22 microseconds. At block
         * granularity that same cable would cost 5.8 ms, which is not
         * feedback but a slapback echo at 172 Hz. That is the whole reason
         * this loop is shaped this way (fm1_rack.h).
         *
         * It runs before master, while the headroom is still there: a folder
         * or a resonant filter needs room above the signal to work in. */
        /* FM1_FX_FORCE_SLOW takes the per-sample path unconditionally. It
         * exists so the two can be rendered against each other and shown to
         * agree, rather than the block form being claimed equivalent. */
#ifdef FM1_FX_FORCE_SLOW
        if (0) {
#else
        if (fx_n && !fx_rack_has_feedback(&rack_, fx_act, fx_n)) {
#endif
            /* No cable points backward, so nothing here is a feedback loop
             * and the per-sample interleave buys nothing: channels are
             * visited in index order and a forward cable already arrives
             * within the same sample. Block form gives the SAME SAMPLES for
             * a fraction of the plumbing (fm1_rack.h). */
            uint8_t live[FM1_FX_CHANS] = { 0 };
            for (uint32_t k = 0; k < fx_n; k++) live[fx_act[k]] = 1;

            for (uint32_t k = 0; k < fx_n; k++) {
                uint32_t n = fx_act[k];
                uint8_t  d = rack_.ch[n].dest;

                fx_chan_block(&rack_.ch[n], rack_.cursor, fxin_[n], chunk);

                if (d == DEST_OUT) {
                    for (uint32_t i = 0; i < chunk * 2u; i++)
                        bus_[i] += fxin_[n][i];
                } else if (DEST_IS_MIX(d)) {
                    int32_t *mb = mixin_[DEST_MIX_IDX(d)];
                    for (uint32_t i = 0; i < chunk * 2u; i++)
                        mb[i] = fx_clamp(mb[i] + fxin_[n][i]);
                } else {
                    /* Into a channel that has not run yet. Only if it is
                     * actually live: an unplugged target is never read, and
                     * never cleared either, so depositing into one would
                     * accumulate without bound for the sake of silence. */
                    uint32_t t = DEST_FX_IDX(d);
                    if (DEST_IS_FX(d) && live[t])
                        for (uint32_t i = 0; i < chunk * 2u; i++)
                            fxin_[t][i] = fx_clamp(fxin_[t][i] + fxin_[n][i]);
                }
            }
            rack_.cursor = (rack_.cursor + chunk) & FM1_FX_DMASK;
        } else if (fx_n) {
            int32_t in_l[FM1_FX_CHANS], in_r[FM1_FX_CHANS];
            for (uint32_t n = 0; n < FM1_FX_CHANS; n++) in_l[n] = in_r[n] = 0;

            for (uint32_t i = 0; i < chunk; i++) {
                for (uint32_t k = 0; k < fx_n; k++) {
                    uint32_t n = fx_act[k];
                    in_l[n] += fxin_[n][2 * i];
                    in_r[n] += fxin_[n][2 * i + 1];
                }
                fx_rack_sample(&rack_, rack_.cursor, fx_act, fx_n,
                               in_l, in_r, &bus_[2 * i], &bus_[2 * i + 1]);
                for (uint32_t m = 0; m < FM1_MIX_CHANS; m++) {
                    if (!mix_live[m]) continue;
                    mixin_[m][2 * i]     += rack_.mix_l[m];
                    mixin_[m][2 * i + 1] += rack_.mix_r[m];
                    rack_.mix_l[m] = rack_.mix_r[m] = 0;
                }
                rack_.cursor = (rack_.cursor + 1u) & FM1_FX_DMASK;
            }
        }

        /* --- master, then the one saturation point ------------------------
         *
         * MASTER scales the bus while it still has headroom, so turning it
         * down genuinely recovers from a hot mix instead of attenuating
         * something already clipped. The soft clip is the only place the
         * instrument distorts; the final add is saturating only to honour
         * Render()'s contract of mixing into whatever the caller left in
         * `out`. */
        /* HAAS: delay ONE channel by a few milliseconds. Under the echo
         * threshold the ear fuses the two arrivals and localises toward the
         * earlier one, so the image widens without the tone changing -- no
         * filter, no EQ, nothing taken away.
         *
         * 0..127 maps to 0..29 ms. The upper end is deliberately past where
         * it stops reading as width and starts reading as a slap: a control
         * that can only do the tasteful thing is a control that cannot do
         * the interesting one.
         *
         * Mono compatibility is the catch, and it is not hidden: summed to
         * mono a delayed copy IS a comb filter. The MIX page's MONO switch
         * exists to let you hear that while the DAW is recording, which is
         * the only honest way to ship this. */
        {
            /* Bipolar: 64 is no delay and either channel can be the early
             * one. A one-directional widener always pushes the image the
             * same way, so using it on two voices stacks them in the same
             * place instead of opening them apart -- which is the whole
             * reason to have it. */
            int32_t  h    = (int32_t)g_[G_HAAS] - 64;
            uint32_t hd   = ((uint32_t)(h < 0 ? -h : h) * 1280u) / 64u;
            int8_t   side = hd ? (h > 0 ? 1 : -1) : 0;

            /* A side change swaps which channel is early, and the line still
             * holds the other channel's audio. Playing that out is a click
             * and a moment of the wrong signal in the wrong ear, so the line
             * is cleared whenever the side changes -- including to zero. */
            if (side != haas_side_) {
                for (uint32_t i = 0; i < 1536u; i++) haas_[i] = 0;
                haas_w_ = 0;
                haas_side_ = side;
            }

            if (side) {
                uint32_t ch = (side > 0) ? 1u : 0u;
                for (uint32_t i = 0; i < chunk; i++) {
                    uint32_t rd = (haas_w_ + 1536u - hd) % 1536u;
                    haas_[haas_w_] = (int16_t)fm1_soft_clip(bus_[i * 2u + ch]);
                    haas_w_ = (haas_w_ + 1u) % 1536u;
                    bus_[i * 2u + ch] = haas_[rd];
                }
            }
        }

        /* --- the mix buses -------------------------------------------
         *
         * Last thing before master: everything that was going to arrive has
         * arrived, and a bus has no destination of its own to feed.
         *
         * PAN here is a BALANCE, not the constant-power pan a voice gets.
         * A voice is one mono signal being placed, so 0.707 either side at
         * centre is right. A bus already carries a stereo image -- several
         * voices, panned -- and running that through the same law would cost
         * it 3 dB for sitting in the middle and squash the image it is
         * carrying. Balance leaves the centre alone and attenuates the side
         * you turn away from. */
        for (uint32_t mb = 0; mb < FM1_MIX_CHANS; mb++) {
            int32_t lvl, pan, gl, gr, pk = 0;
            if (!mix_live[mb]) { if (mixmeter_[mb]) mixmeter_[mb]--; continue; }

            lvl = g_[G_MIX(mb, G_MIX_LEVEL)];
            pan = g_[G_MIX(mb, G_MIX_PAN)];
            gl  = pan <= 64 ? Q12_ONE : ((127 - pan) * Q12_ONE) / 63;
            gr  = pan >= 64 ? Q12_ONE : ( pan        * Q12_ONE) / 64;

            for (uint32_t i = 0; i < chunk; i++) {
                int32_t l = (mixin_[mb][2 * i]     * lvl) >> 7;
                int32_t r = (mixin_[mb][2 * i + 1] * lvl) >> 7;
                int32_t a;
                l = (l * gl) >> Q12_BITS;
                r = (r * gr) >> Q12_BITS;
                bus_[2 * i]     += l;
                bus_[2 * i + 1] += r;
                a = l < 0 ? -l : l; if (a > pk) pk = a;
                a = r < 0 ? -r : r; if (a > pk) pk = a;
            }
            {
                uint8_t mm = (uint8_t)(pk >= Q15_ONE ? 127
                                       : ((int32_t)pk * 127) / Q15_ONE);
                mixmeter_[mb] = mm;
            }
        }

        int32_t m = g_[G_MASTER];
        for (uint32_t i = 0; i < chunk * 2u; i++) {
            int32_t s = bus_[i];
            if (m != 127) s = (s * m) >> 7;
            out[i] = fm1_q31_to_q15_sat((int32_t)out[i] + fm1_soft_clip(s));
        }

        out += chunk * 2u;
        n -= chunk;
        blocks_++;
    }
}

}  // namespace fm1
