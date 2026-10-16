/* SPDX-License-Identifier: MIT */
#include "fm1_drone.h"

namespace fm1 {

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
        fm1_comb_init(&d.comb);
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
    fm1_comb_set_from_inc(&d.comb, inc, comb_ratio_q12(d.p[P_COMB_TUNE]));
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
    case P_OCTAVE: case P_DETUNE: case P_COMB_TUNE:
        ApplyPitch(voice);
        break;
    case P_COMB_FB:
        /* 0..127 -> 0..0.945 in Q15; never unity, or it never decays */
        fm1_comb_set_feedback(&d.comb, ((int32_t)value * 31000) / 127);
        break;
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

            /* --- per-sample: comb, level, pan ---------------------------- */
            int32_t mix = d.p[P_COMB_MIX];              /* 0..127 */
            int32_t lvl = d.p[P_LEVEL];                 /* 0..127 */
            int32_t pan = d.p[P_PAN];                   /* 0..127, 64 centre */

            /* Constant-ish power without a sqrt: linear is fine for a drone
             * bed and costs two multiplies. */
            int32_t gl = (lvl * (127 - pan)) >> 7;
            int32_t gr = (lvl * pan) >> 7;
            /* scale so a centred voice is unity-ish, then leave headroom for
             * four voices summing */
            gl = (gl << 1) / FM1_VOICES;
            gr = (gr << 1) / FM1_VOICES;

            for (uint32_t i = 0; i < chunk; i++) {
                int32_t s = d.scratch[i];
                if (mix) {
                    int32_t c = fm1_comb_tick(&d.comb, (q15_t)s);
                    s = s + (((c - s) * mix) >> 7);
                } else {
                    /* keep the line moving so switching the comb in does not
                     * read a stale buffer */
                    (void)fm1_comb_tick(&d.comb, (q15_t)s);
                }
                int32_t l = (s * gl) >> 7;
                int32_t r = (s * gr) >> 7;
                out[2 * i]     = fm1_q31_to_q15_sat(out[2 * i] + l);
                out[2 * i + 1] = fm1_q31_to_q15_sat(out[2 * i + 1] + r);
            }
        }

        /* --- master ------------------------------------------------------ */
        int32_t m = g_[G_MASTER];
        if (m != 127) {
            for (uint32_t i = 0; i < chunk * 2u; i++)
                out[i] = (int16_t)((out[i] * m) >> 7);
        }

        out += chunk * 2u;
        n -= chunk;
        blocks_++;
    }
}

}  // namespace fm1
