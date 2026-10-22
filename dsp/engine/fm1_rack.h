/* SPDX-License-Identifier: MIT
 *
 * The FX patchbay: four effect channels, each with one effect and one cable.
 *
 * This is a PATCHBAY, not an insert rack. The question a patch answers is not
 * "what order are the blocks in" but "where does this output go" -- the
 * modular question. Every node, voice or effect, owns exactly one destination,
 * like a cable leaving a jack:
 *
 *     VOICE 1 --> FX1        FX1 [COMB]   --> FX2
 *     VOICE 2 --> FX1        FX2 [CHORUS] --> OUT
 *     VOICE 3 --> OUT        FX3 [OFF]    --> ---
 *     VOICE 4 --> ---        FX4 [OFF]    --> ---
 *
 * A channel has no input control. Whatever is routed TO it sums at its input;
 * routing is what fills it. Set a channel's destination to OFF and everything
 * feeding it goes silent, because the cable is unplugged -- no mute logic and
 * no special case.
 *
 * An earlier version of this file was a reorderable chain of slots with a
 * "PAR" flag to fake branching. That was the wrong model: a chain is a mixing
 * desk's insert rack, and branching is not a special case in a patchbay, it is
 * just two cables pointing at the same place.
 *
 * --- FEEDBACK ---------------------------------------------------------------
 *
 * Any cable is legal, including FX2 -> FX1 and FX1 -> FX1.
 *
 * Nodes are visited in index order within each sample. A cable pointing
 * FORWARD lands in its target's accumulator before that target runs, so it
 * arrives the same sample with no delay at all. A cable pointing BACKWARD
 * lands after the target has already run, so it is picked up on the next
 * sample -- one sample, 22 microseconds.
 *
 * That falls out of the evaluation order; there is no cycle detection and no
 * graph sort. It is also why this is evaluated per SAMPLE and not per block:
 * at block granularity the same cable would cost 256 samples, and a 5.8 ms
 * loop is not feedback, it is a slapback echo welded into the signal path at
 * 172 Hz. VCV Rack behaves the same way and for the same reason.
 *
 * What feedback does still need is a LEVEL guard. A loop with gain >= 1 grows
 * without bound, and int32 wraps long before anything musical happens --
 * wrapping turns a scream into white noise, which is the one failure mode
 * that is not fun. Accumulators are clamped, so a runaway saturates into the
 * soft clip and screams instead.
 *
 * C++ only: FX_LP/FX_HP embed braids::Svf.
 */
#ifndef FM1_RACK_H
#define FM1_RACK_H

#include "../mi/braids/svf.h"
#include "../shared/fm1_fixed.h"

/* The effect types and the routing targets, with their names. They live in
 * their own header because this one embeds braids::Svf and is therefore C++
 * only, while the firmware's UI is C and has to put an effect name and a
 * destination on the screen.
 *
 * Deliberately NOT among them: reverb and delay. The output goes to a DAW
 * over USB audio, where both are better and free; the RAM buys things a DAW
 * does not hand you for nothing (SIGNAL_PATH.md section 6). */
#include "fm1_route.h"

#define FM1_FX_DELAY   1024u            /* 23 ms at 44.1 kHz */
#define FM1_FX_DMASK   (FM1_FX_DELAY - 1u)

/* Accumulator clamp. The bus legitimately reaches about 4.0 with four voices
 * at full level; 8.0 is double that, so normal playing never touches this and
 * only a feedback loop can. */
#define FM1_FX_CEIL  (8 * 32768)

namespace fm1 {

struct fx_chan_t {
    uint8_t  type;
    uint8_t  p1, p2;        /* meaning is per type; see fx_chan_tick */
    uint8_t  mix;           /* dry/wet for this channel, 0..127 */
    uint8_t  dest;          /* DEST_* -- the one cable out */

    uint32_t phase;         /* chorus LFO */
    braids::Svf svf[2];     /* FX_LP / FX_HP, one per channel side */

    /* Input accumulator. Persists ACROSS samples, which is what gives a
     * backward cable its one-sample delay for free. */
    int32_t  acc[2];

    /* int32, not int16.
     *
     * The comb taught this one: its delay line stored Q15 and saturated
     * INSIDE the loop, before any output gain, so it never showed as clipping
     * in a rendered file -- it just sounded harsh, and cost a day to find.
     * This sits on a bus carrying up to 4.0, so an int16 line would clip on
     * every loud patch for exactly the same invisible reason. */
    int32_t  dl[2][FM1_FX_DELAY];
};

struct fx_rack_t {
    fx_chan_t ch[FM1_FX_CHANS];
    uint32_t  cursor;       /* delay write position, continuous across blocks */
};

static inline void fx_chan_init(fx_chan_t *c)
{
    c->type = FX_NONE;
    c->p1 = 64; c->p2 = 64;
    c->mix = 127;
    c->dest = DEST_OFF;
    c->phase = 0;
    c->acc[0] = c->acc[1] = 0;
    c->svf[0].Init();
    c->svf[1].Init();
    for (uint32_t s = 0; s < 2; s++)
        for (uint32_t i = 0; i < FM1_FX_DELAY; i++) c->dl[s][i] = 0;
}

static inline void fx_rack_init(fx_rack_t *r)
{
    for (uint32_t i = 0; i < FM1_FX_CHANS; i++) fx_chan_init(&r->ch[i]);
    r->cursor = 0;
}

static inline void fx_chan_set_type(fx_chan_t *c, uint8_t t)
{
    if (t >= FX_NTYPES || t == c->type) return;
    uint8_t p1 = c->p1, p2 = c->p2, mix = c->mix, dest = c->dest;
    fx_chan_init(c);            /* state across a type change is meaningless */
    c->type = t;
    c->p1 = p1; c->p2 = p2; c->mix = mix; c->dest = dest;
    if (t == FX_LP || t == FX_HP) {
        for (int s = 0; s < 2; s++) {
            c->svf[s].set_mode(t == FX_HP ? braids::SVF_MODE_HP
                                          : braids::SVF_MODE_LP);
            c->svf[s].set_frequency((int16_t)((int32_t)c->p1 << 7));
            c->svf[s].set_resonance((int16_t)((int32_t)c->p2 * 258));
        }
    }
}

static inline void fx_chan_set_p1(fx_chan_t *c, uint8_t v)
{
    c->p1 = v;
    if (c->type == FX_LP || c->type == FX_HP)
        for (int s = 0; s < 2; s++)
            c->svf[s].set_frequency((int16_t)((int32_t)v << 7));
}

static inline void fx_chan_set_p2(fx_chan_t *c, uint8_t v)
{
    c->p2 = v;
    if (c->type == FX_LP || c->type == FX_HP)
        for (int s = 0; s < 2; s++)
            c->svf[s].set_resonance((int16_t)((int32_t)v * 258));
}

static inline void fx_chan_set_dest(fx_chan_t *c, uint8_t d)
{
    if (d < DEST_NDEST) c->dest = d;
}

/* --- the effects ----------------------------------------------------------- */

/* Wavefolder. Drive, then reflect back into range instead of clipping -- a
 * clipper removes harmonics, a folder adds them, which is the point.
 *
 * Repeated reflection IS a triangle wave of period 4*32768, so it is computed
 * directly rather than iterated. The first version reflected in a loop capped
 * at four passes; that is correct for the drive range the knob can reach, and
 * wrong for anything louder -- a test at 3x drive on a +-4.0 bus walked
 * straight out of Q15, because it needed six. A bounded loop is a silent
 * wrong answer outside its bound and an ISR hang without one; this has
 * neither failure mode and is cheaper.
 *
 * The period is 2^17, so the wrap is a mask -- no division.
 *
 *   u in [0, 32768)        y = u            rising
 *   u in [32768, 98304)    y = 65536 - u    falling through zero
 *   u in [98304, 131072)   y = u - 131072   rising back to zero
 */
static inline int32_t fx_fold1(int32_t x, int32_t drive_q12, int32_t bias)
{
    x = (int32_t)(((int64_t)x * drive_q12) >> Q12_BITS);
    x += bias;

    uint32_t u = (uint32_t)x & 0x1FFFFu;
    int32_t  y;
    if (u < 32768u)      y = (int32_t)u;
    else if (u < 98304u) y = 65536 - (int32_t)u;
    else                 y = (int32_t)u - 131072;

    return y > Q15_ONE ? Q15_ONE : y;   /* the single peak code at +32768 */
}

static inline void fx_chan_tick(fx_chan_t *c, uint32_t w,
                                int32_t il, int32_t ir,
                                int32_t *ol, int32_t *orr)
{
    int32_t wl = il, wr = ir;

    switch (c->type) {
    case FX_CHORUS: {
        /* Two taps 90 degrees apart, which is what makes it stereo without a
         * second LFO. p1 is rate, p2 is depth. */
        /* One phase cycle is 2^32, so Hz = inc * 44100 / 2^32 and one hertz
         * is 97391. The old constants -- 200000 + p1*26000 -- were therefore
         * 2.05 Hz to 35.96 Hz, not the 0.05..0.8 the comment claimed: forty
         * times too fast, with the DEFAULT p1 of 64 landing on 19.1 Hz. At
         * that rate the delay sweeps its whole range in 26 ms and reverses
         * hard at each apex, which is a warble, not a chorus.
         *
         * rings/dsp/fx/chorus.h runs its two LFOs at 0.184 and 0.239 Hz. */
        uint32_t inc = 4870u + (uint32_t)c->p1 * 575u;       /* 0.05..0.8 Hz */
        c->phase += inc;

        int32_t depth = (int32_t)c->p2;                      /* 0..127 */
        for (int s = 0; s < 2; s++) {
            uint32_t ph = c->phase + (s ? 0x40000000u : 0u); /* +90 degrees */
            uint32_t p16 = ph >> 16;                         /* 0..65535 */
            int32_t  tri = (int32_t)(p16 < 32768u ? p16 : 65535u - p16);

            /* 4..20 ms of delay, modulated. Q8 for a fractional tap. */
            int32_t d_q8 = (180 << 8) + ((tri * depth * 3) >> 8);
            uint32_t di = (uint32_t)(d_q8 >> 8), df = (uint32_t)(d_q8 & 0xFF);
            if (di > FM1_FX_DELAY - 2u) di = FM1_FX_DELAY - 2u;

            uint32_t r0 = (w - di) & FM1_FX_DMASK;
            uint32_t r1 = (r0 - 1u) & FM1_FX_DMASK;
            int32_t a = c->dl[s][r0], b = c->dl[s][r1];
            int32_t v = a + (((b - a) * (int32_t)df) >> 8);
            if (s) wr = v; else wl = v;
        }
        break;
    }
    case FX_FOLD: {
        int32_t drive = Q12_ONE + (int32_t)c->p1 * 24;       /* 1.0 .. ~1.75x */
        int32_t bias  = ((int32_t)c->p2 - 64) * 64;
        wl = fx_fold1(il, drive, bias);
        wr = fx_fold1(ir, drive, bias);
        break;
    }
    case FX_WIDTH: {
        /* Mid/side. p1 = 0 is mono, 64 is unchanged, 127 is about 2x wide.
         * The side gain is p1/64, so 64 maps to exactly 1.0 -- an earlier
         * `* 2` here made the centre detent 2x wide, which the unity test
         * caught. */
        int32_t m  = (il + ir) >> 1;
        int32_t sd = (il - ir) >> 1;
        sd = (sd * (int32_t)c->p1) >> 6;
        wl = m + sd;
        wr = m - sd;
        break;
    }
    case FX_LP: case FX_HP:
        /* SCALED INTO THE FILTER'S RANGE, AND BACK OUT.
         *
         * braids::Svf clamps its own integrators to int16 (CLIP in svf.h),
         * but this bus deliberately carries FM1_FX_CEIL = 8.0 -- four voices
         * at full level reach 4.0 and the headroom is the whole point of the
         * int32 bus. So the filter saturated at an eighth of what it was
         * handed: an 18 dB hard clip, inside the loop, before any output
         * gain, on a channel the panel calls LOWPASS.
         *
         * That is verbatim the lesson this file already records for the comb
         * at the top of the header -- "it saturated INSIDE the loop, before
         * any output gain, so it never showed as clipping in a rendered
         * file" -- and then repeats two lines later. */
        wl = (int32_t)c->svf[0].Process(il >> 3) << 3;
        wr = (int32_t)c->svf[1].Process(ir >> 3) << 3;
        break;
    default:
        break;      /* FX_NONE: the channel is a wire, wet is dry */
    }

    /* The delay line is written with the DRY input, always -- so a channel
     * switched on mid-patch reads recent audio rather than silence, or
     * whatever was sitting in it a minute ago. */
    c->dl[0][w] = il;
    c->dl[1][w] = ir;

    if (c->mix >= 127) { *ol = wl; *orr = wr; return; }
    *ol  = il + (((wl - il) * (int32_t)c->mix) >> 7);
    *orr = ir + (((wr - ir) * (int32_t)c->mix) >> 7);
}

static inline int32_t fx_clamp(int32_t v)
{
    if (v >  FM1_FX_CEIL) return  FM1_FX_CEIL;
    if (v < -FM1_FX_CEIL) return -FM1_FX_CEIL;
    return v;
}

/* Deposit a stereo sample at `dest`.
 *
 * `in` holds the per-channel input buffers for THIS sample, so a forward
 * cable (to a channel not yet visited) arrives with no delay. A backward or
 * self cable goes to the channel's accumulator instead, where it waits for
 * the next sample. `out` is the master bus.
 */
static inline void fx_deposit(fx_rack_t *r, uint8_t dest, uint32_t visiting,
                              int32_t *in_l, int32_t *in_r,
                              int32_t l, int32_t rr,
                              int32_t *out_l, int32_t *out_r)
{
    if (dest == DEST_OFF) return;
    if (dest == DEST_OUT) { *out_l += l; *out_r += rr; return; }

    uint32_t t = (uint32_t)(dest - DEST_FX1);
    if (t >= FM1_FX_CHANS) return;

    if (t > visiting) {                 /* forward: same sample */
        in_l[t] += l;
        in_r[t] += rr;
    } else {                            /* backward or self: next sample */
        r->ch[t].acc[0] = fx_clamp(r->ch[t].acc[0] + l);
        r->ch[t].acc[1] = fx_clamp(r->ch[t].acc[1] + rr);
    }
}

/* One sample through the patchbay.
 *
 * `in_l`/`in_r` carry what the voices routed to each channel this sample, and
 * are consumed here. `out_l`/`out_r` already hold whatever the voices sent
 * straight to OUT, and receive everything the channels send there.
 */
/* `act` lists the PLUGGED channels, in ascending index order, gathered once
 * per block by the caller. Walking all four here instead cost about 50,000
 * cycles a block with a single effect patched -- three quarters of the work
 * being done to discover there was nothing to do.
 *
 * The indices in `act` are absolute, which matters: forward versus backward
 * is decided by channel NUMBER, not by position in this list, so the
 * one-sample rule stays what the panel says it is even when the channels in
 * between are unplugged.
 */
static inline void fx_rack_sample(fx_rack_t *r, uint32_t w,
                                  const uint8_t *act, uint32_t n_act,
                                  int32_t *in_l, int32_t *in_r,
                                  int32_t *out_l, int32_t *out_r)
{
    for (uint32_t k = 0; k < n_act; k++) {
        uint32_t n = act[k];
        fx_chan_t *c = &r->ch[n];

        int32_t il = fx_clamp(in_l[n] + c->acc[0]);
        int32_t ir = fx_clamp(in_r[n] + c->acc[1]);
        in_l[n] = in_r[n] = 0;
        c->acc[0] = c->acc[1] = 0;

        int32_t ol, orr;
        fx_chan_tick(c, w, il, ir, &ol, &orr);
        fx_deposit(r, c->dest, n, in_l, in_r, ol, orr, out_l, out_r);
    }
}

/* Gather the plugged channels. Returns how many, and fills `act`. */
static inline uint32_t fx_rack_active(const fx_rack_t *r, uint8_t *act)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < FM1_FX_CHANS; i++)
        if (r->ch[i].dest != DEST_OFF) act[n++] = (uint8_t)i;
    return n;
}

}  // namespace fm1

#endif /* FM1_RACK_H */
