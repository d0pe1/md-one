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
static_assert(FX_NTYPES - 1 == 5,
              "effect count changed -- update G_FX*_TYPE max in fm1_params.h");
static_assert(DEST_NDEST - 1 == 5,
              "destination count changed -- update P_DEST and G_FX*_DEST max");
static_assert(DEST_FX1 + FM1_FX_CHANS == DEST_OUT,
              "DEST_* is no longer DEST_FX1..FX4 then DEST_OUT -- the engine "
              "indexes channels by (dest - DEST_FX1)");
static_assert(G_FX(3, G_FX_DEST) == NGPARAM - 1,
              "the patchbay parameters are not the tail of the global table");

/* Detune and drift are applied in 1/128-semitone units, Braids' pitch unit.
 * DETUNE is 0..127 around a centre of 64, spanning +-50 cents. */
static const int32_t kDetuneSpan = 64;      /* 50 cents = 64 units of 1/128 */

/* ~0.5 s of red at 256 frames per block, 44.1 kHz. */
static const uint8_t kClipHoldBlocks = 86;

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
    fx_rack_init(&rack_);

    for (uint8_t v = 0; v < FM1_VOICES; v++) {
        DroneVoice &d = v_[v];
        d.osc.Init();
        for (uint8_t fi = 0; fi < FM1_FILTERS; fi++) fm1_filter_init(&d.f[fi]);
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
    v_[voice].gate = on ? 1 : 0;
    fm1_env_gate(&v_[voice].env, v_[voice].gate);
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

    case P_DRIVE:   d.drive_q12 = value ? fm1_drive_gain(value) : Q12_ONE; break;
    case P_ATTACK:  fm1_env_set_attack(&d.env, value);  break;
    case P_RELEASE: fm1_env_set_release(&d.env, value); break;

    default:
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
    g_[param] = value;
    if (param == G_SPREAD)
        for (uint8_t v = 0; v < FM1_VOICES; v++) ApplyPitch(v);

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
            /* MONO overrides PAN rather than resetting it, so switching it
             * off returns the voice to exactly where it was in the image.
             * Centre is 63.5/127, taken as 64 and 63 -- one LSB of asymmetry
             * is inaudible and keeps the two gains summing to the same total
             * a panned voice has. */
            int32_t gl_q12, gr_q12;
            if (d.p[P_MONO]) {
                gl_q12 = (64 * Q12_ONE) / 127;
                gr_q12 = (63 * Q12_ONE) / 127;
            } else {
                gl_q12 = ((127 - pan) * Q12_ONE) / 127;
                gr_q12 = ( pan        * Q12_ONE) / 127;
            }

            /* Where this voice's cable goes. DEST_OFF means the voice is
             * rendered and discarded -- it still costs its cycles, which is
             * correct: unplugging a cable does not unplug the oscillator, and
             * re-plugging it must not restart the sound. */
            uint8_t  dest = d.p[P_DEST];
            if (dest == DEST_OFF) continue;
            int32_t *sink = (dest == DEST_OUT)
                          ? bus_
                          : fxin_[dest - DEST_FX1];

            /* Asked once per block, not once per sample. Both of these are
             * flat on most voices most of the time, and testing inside the
             * loop made them cost real cycles for doing nothing. */
            const bool eq_on    = !fm1_eq_idle(&d.eq);
            const bool drive_on = d.drive_q12 > Q12_ONE;

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
                sink[2 * i]     += (s * gl_q12) >> Q12_BITS;
                sink[2 * i + 1] += (s * gr_q12) >> Q12_BITS;
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
        if (fx_n) {
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
