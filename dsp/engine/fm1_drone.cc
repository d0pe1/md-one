/* SPDX-License-Identifier: MIT */
#include "fm1_drone.h"

namespace fm1 {

/* fm1_params.h writes the filter mode range as a literal so it does not have
 * to include fm1_filter.h (which would drag braids::Svf in and make it C++
 * only). This is where the two are held together.
 *
 * It compares against the literal rather than FM1_PDEF[...].max because that
 * table is `static const`, not constexpr, and C++ will not read it in a
 * constant expression. Adding a mode without widening the parameter's max
 * still fails here, which is the mistake worth catching. */
static_assert(F_NMODES - 1 == 5,
              "filter mode count changed -- update P_F1_TYPE/P_F2_TYPE max in fm1_params.h");
static_assert(F_OFF == 0,
              "F_OFF moved -- update P_F1_TYPE/P_F2_TYPE def in fm1_params.h");

/* Detune and drift are applied in 1/128-semitone units, Braids' pitch unit.
 * DETUNE is 0..127 around a centre of 64, spanning +-50 cents. */
static const int32_t kDetuneSpan = 64;      /* 50 cents = 64 units of 1/128 */

/* Comb tune: parameter 0..127 maps to a delay ratio in Q12, 4096 being the
 * note itself. 64 -> unison, 0 -> two octaves up, 127 -> two octaves down. */
static inline int32_t comb_ratio_q12(uint8_t v)
{
    /* 1024 (two octaves up) .. 4096 (unison) .. 16384 (two octaves down),
     * geometric in feel but computed with shifts and a linear blend. */
    if (v <= 64) {
        /* 0..64  ->  1024..4096 */
        return 1024 + ((int32_t)v * (4096 - 1024)) / 64;
    }
    /* 65..127 -> 4096..16384 */
    return 4096 + ((int32_t)(v - 64) * (16384 - 4096)) / 63;
}

void Drone::Init(uint32_t seed)
{
    for (uint8_t i = 0; i < NPARAM; i++) {
        /* defaults from the table, so the engine and the UI agree */
        for (uint8_t v = 0; v < FM1_VOICES; v++) v_[v].p[i] = FM1_PDEF[i].def;
    }
    for (uint8_t i = 0; i < NGPARAM; i++) g_[i] = FM1_GDEF[i].def;

    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        DroneVoice &d = v_[v];
        d.osc.Init();
        for (uint8_t fi = 0; fi < FM1_FILTERS; fi++) fm1_filter_init(&d.f[fi]);
        /* a different seed per voice, or all four drift in lockstep */
        fm1_drift_init(&d.drift, seed + v * 0x9E3779B9u);
        d.note = 36 + 12 * v;          /* spread across octaves by default */
        d.gate = 0;
        d.osc.set_shape((braids::MacroOscillatorShape)d.p[P_SHAPE]);
        ApplyPitch(v);
    }
    blocks_ = 0;
}

void Drone::ApplyPitch(uint8_t v)
{
    DroneVoice &d = v_[v];

    int32_t pitch = (int32_t)d.note * kPitchPerSemitone;

    /* OCTAVE is 0..8 with 4 as centre */
    pitch += ((int32_t)d.p[P_OCTAVE] - 4) * 12 * kPitchPerSemitone;

    /* DETUNE, bipolar around 64 */
    pitch += ((int32_t)d.p[P_DETUNE] - 64) * kDetuneSpan / 64;

    /* SPREAD fans the voices apart, voice 0 lowest */
    int32_t spread = (int32_t)g_[G_SPREAD];
    if (spread) {
        int32_t off = ((int32_t)v - (FM1_VOICES - 1) / 2) * spread * kDetuneSpan / 127;
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
        /* MIDI note -> Hz in Q16, by table-free octave shifting of A4.
         * 440 Hz in Q16 is 28835840; note 69 is A4. */
        int32_t semis = (pitch / kPitchPerSemitone) - 69;
        int32_t oct = semis / 12, rem = semis % 12;
        if (rem < 0) { rem += 12; oct -= 1; }
        /* 2^(n/12) in Q12 for n = 0..11 */
        static const uint16_t kSemi[12] = {
            4096, 4339, 4596, 4868, 5157, 5462,
            5786, 6129, 6492, 6877, 7286, 7719
        };
        uint32_t f = (uint32_t)((440u << 16) >> 12) * kSemi[rem];   /* Q16 */
        if (oct >= 0) f <<= (uint32_t)oct; else f >>= (uint32_t)(-oct);
        hz_q16 = (q16_t)f;
    }
    uint32_t inc = fm1_phase_inc(hz_q16, FM1_K_44100);
    fm1_filter_set_key(&d.f[0], (int16_t)pitch, inc, comb_ratio_q12(d.p[P_F1_CUT]));
    fm1_filter_set_key(&d.f[1], (int16_t)pitch, inc, comb_ratio_q12(d.p[P_F2_CUT]));
}

void Drone::NoteOn(uint8_t voice, uint8_t note)
{
    if (voice >= FM1_VOICES) return;
    v_[voice].note = (int16_t)note;
    v_[voice].gate = 1;
    ApplyPitch(voice);
    v_[voice].osc.Strike();
}

void Drone::NoteOff(uint8_t voice)
{
    if (voice >= FM1_VOICES) return;
    v_[voice].gate = 0;
}

void Drone::SetGate(uint8_t voice, uint8_t on)
{
    if (voice >= FM1_VOICES) return;
    v_[voice].gate = on ? 1 : 0;
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
    if (d.p[param] == value) return;
    d.p[param] = value;

    switch (param) {
    case P_SHAPE:
        d.osc.set_shape((braids::MacroOscillatorShape)value);
        break;
    case P_OCTAVE: case P_DETUNE:
        ApplyPitch(voice);
        break;

    /* CUT drives the comb's delay, so it has to go through ApplyPitch, which
     * is where the phase increment the comb tunes from is computed. For the
     * SVF modes set_cut alone would do; doing both is a few cycles on a knob
     * turn and keeps one path instead of two. */
    case P_F1_CUT: fm1_filter_set_cut(&d.f[0], value); ApplyPitch(voice); break;
    case P_F2_CUT: fm1_filter_set_cut(&d.f[1], value); ApplyPitch(voice); break;

    case P_F1_TYPE: fm1_filter_set_mode(&d.f[0], value); ApplyPitch(voice); break;
    case P_F2_TYPE: fm1_filter_set_mode(&d.f[1], value); ApplyPitch(voice); break;

    case P_F1_RES:  fm1_filter_set_res(&d.f[0], value); break;
    case P_F2_RES:  fm1_filter_set_res(&d.f[1], value); break;
    case P_F1_MIX:  fm1_filter_set_mix(&d.f[0], value); break;
    case P_F2_MIX:  fm1_filter_set_mix(&d.f[1], value); break;

    default:
        break;      /* read directly at render time */
    }
}

void Drone::SetGlobal(uint8_t param, uint8_t value)
{
    if (param >= NGPARAM) return;
    if (value > FM1_GDEF[param].max) value = FM1_GDEF[param].max;
    g_[param] = value;
    if (param == G_SPREAD)
        for (uint8_t v = 0; v < FM1_VOICES; v++) ApplyPitch(v);
}

uint8_t Drone::GetParam(uint8_t voice, uint8_t param) const
{
    if (voice >= FM1_VOICES || param >= NPARAM) return 0;
    return v_[voice].p[param];
}

void Drone::Render(int16_t *out, uint32_t n)
{
    while (n) {
        uint32_t chunk = (n > FM1_BLOCK) ? FM1_BLOCK : n;

        for (uint32_t i = 0; i < chunk * 2u; i++) bus_[i] = 0;

        for (uint8_t vi = 0; vi < FM1_VOICES; vi++) {
            DroneVoice &d = v_[vi];
            if (!d.gate) continue;

            /* --- block-rate control -------------------------------------
             * Drift, timbre and pitch move once per block. At 5.8 ms that is
             * far finer than anything audible here, and 256x cheaper than
             * doing it per sample. */
            /* One drift source, two attenuverters -- Braids' FM and
             * MODULATION, which on the module scale CV inputs. Run the drift
             * at full depth and scale per destination, so the two knobs are
             * independent and either can be closed. */
            int32_t dr = fm1_drift_block(&d.drift, d.p[P_DRIFT_RATE], 127);
            int32_t fm_amt  = (int32_t)d.p[P_FM]  - 64;     /* -64..+63 */
            int32_t mod_amt = (int32_t)d.p[P_MOD] - 64;

            /* Drift moves pitch AND timbre: the tuning wanders and the
             * spectrum breathes, which together read as "alive". Pitch is the
             * one that matters most -- against the fixed comb and the other
             * voices it produces slow beating. */

            /* FM: drift into pitch. +-50 cents at full deflection -- enough
             * to beat against the fixed comb and the other voices, not enough
             * to sound out of tune. */
            int32_t dp = (((dr * 64) >> 15) * fm_amt) >> 6;
            int32_t pitch = d.pitch_base + dp;
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
            int32_t timbre = (int32_t)d.p[P_TIMBRE] << 8;
            timbre += (((dr >> 2) * mod_amt) >> 6);
            if (timbre < 0)     timbre = 0;
            if (timbre > 32767) timbre = 32767;

            d.osc.set_parameters((int16_t)timbre,
                                 (int16_t)((int32_t)d.p[P_COLOR] << 8));

            d.osc.Render(d.scratch, chunk);

            /* --- per-sample: F1, VCA, F2, pan ---------------------------- */
            int32_t lvl = d.p[P_LEVEL];                 /* 0..127 */
            int32_t pan = d.p[P_PAN];                   /* 0..127, 64 centre */

            /* Voice gain is level x pan and NOTHING else.
             *
             * It used to be divided by FM1_VOICES, which made every voice
             * quieter as soon as another existed -- turning a voice off
             * changed the level of the rest. Unity is one voice at full level,
             * centre-panned; four of those reach 4.0 on the bus, which is
             * exactly the two bits of headroom int32 is here to carry.
             *
             * Linear pan law, not constant-power: a sqrt is not available and
             * a drone bed does not need the 3 dB centre lift. Q12 keeps the
             * quantisation well below a step of the 0..127 knob.
             *
             * LEVEL is NOT here any more -- the VCA applies it inside the
             * sample loop, between the two filters, so that F2 hears a voice
             * that has already been faded. Applying it in both places would
             * square it. */
            int32_t gl_q12 = ((127 - pan) * Q12_ONE) / 127;
            int32_t gr_q12 = ( pan        * Q12_ONE) / 127;

            for (uint32_t i = 0; i < chunk; i++) {
                /* OSC -> F1 -> VCA -> F2. The VCA sits BETWEEN the filters by
                 * design (SIGNAL_PATH.md OPEN-2): F2 in a resonant mode rings
                 * on after the voice stops, like a struck string. The cost is
                 * that muting leaves a tail, which is accepted deliberately --
                 * a hard kill is the job of a separate control, not of this
                 * ordering. */
                int32_t s = fm1_filter_tick(&d.f[0], d.scratch[i]);
                s = (s * lvl) >> 7;                     /* VCA */
                s = fm1_filter_tick(&d.f[1], (q15_t)s);

                /* No saturation here. The bus has room; limiting once at the
                 * end is the whole point. */
                bus_[2 * i]     += (s * gl_q12) >> Q12_BITS;
                bus_[2 * i + 1] += (s * gr_q12) >> Q12_BITS;
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
