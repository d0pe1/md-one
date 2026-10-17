/* SPDX-License-Identifier: MIT
 *
 * The global FX rack: ordered slots on the stereo mix bus.
 *
 *     MIX BUS (int32) -> [ slot 1 .. slot 4 ] -> master -> soft clip
 *
 * Serum's shape, for Serum's reason (docs/SIGNAL_PATH.md section 5b): the rack
 * is a LINE, not a graph. You change the ORDER, never the connections. That is
 * slots evaluated in a plain for-loop -- no bitmask of sources, no cycle
 * detection, no topological sort, no allocation -- and on a two-line screen it
 * is one Page per slot instead of a patching matrix.
 *
 * These blocks live here rather than per voice because they want a summed
 * signal. Four independent chorus instances summed afterwards is four times
 * the cost for a worse result than one chorus on the mix. The pitch-tracking
 * blocks went the other way, into the per-voice filter (fm1_filter.h).
 *
 * THE ONE THING A CHAIN CANNOT EXPRESS is two blocks fed the same input and
 * summed. That comes back as a per-slot PAR flag -- "take the same input as
 * the slot above me; our outputs add" -- which is one bit and one scratch
 * pair, not a routing matrix:
 *
 *     1  FOLD
 *     2  CHORUS  [PAR]   <- both read slot 1's input, outputs sum
 *     3  WIDTH           <- reads that sum
 *
 * Chains of PAR give N-way parallel. A slot can still only ever read what is
 * above it, so the graph stays acyclic by construction.
 *
 * C++ only: FX_LP/FX_HP embed braids::Svf.
 */
#ifndef FM1_RACK_H
#define FM1_RACK_H

#include "../mi/braids/svf.h"
#include "../shared/fm1_fixed.h"

/* Block types. Deliberately NOT here: reverb and delay. The output goes to a
 * DAW over USB audio, where both are better and free; the RAM buys things a
 * DAW does not hand you for nothing (SIGNAL_PATH.md section 6). */
enum {
    FX_NONE = 0,
    FX_CHORUS,      /* modulated short delay, stereo by construction */
    FX_FOLD,        /* wavefolder: drive, then reflect */
    FX_WIDTH,       /* mid/side width */
    FX_LP,          /* state-variable low pass, stereo */
    FX_HP,
    FX_NTYPES
};

static const char *const FM1_FXNAME[FX_NTYPES] = {
    "OFF", "CHORUS", "FOLD", "WIDTH", "LOWPASS", "HIGHPASS"
};

#define FM1_FX_SLOTS   4
#define FM1_FX_DELAY   1024u            /* 23 ms at 44.1 kHz */
#define FM1_FX_DMASK   (FM1_FX_DELAY - 1u)

#define FM1_FX_PAR     0x01             /* flags bit 0 */

namespace fm1 {

struct fx_slot_t {
    uint8_t  type;
    uint8_t  p1, p2;        /* meaning is per type; see fx_slot_tick */
    uint8_t  mix;           /* dry/wet for this slot, 0..127 */
    uint8_t  flags;         /* FM1_FX_PAR */

    uint32_t phase;         /* chorus LFO */
    braids::Svf svf[2];     /* FX_LP / FX_HP, one per channel */

    /* int32, not int16.
     *
     * The comb taught this one: its delay line stored Q15 and saturated
     * INSIDE the loop, before any output gain, so it never showed as clipping
     * in a rendered file -- it just sounded harsh, and cost a day to find.
     * The rack sits on a bus carrying up to 4.0, so an int16 line here would
     * clip on every loud patch for exactly the same invisible reason. 8 KiB a
     * slot is the cheap way not to repeat that. */
    int32_t  dl[2][FM1_FX_DELAY];
};

struct fx_rack_t {
    fx_slot_t slot[FM1_FX_SLOTS];
    int32_t   par[2];       /* the input a PAR run shares, L and R */
    uint32_t  cursor;       /* write position, continuous across blocks */
};

static inline void fx_slot_init(fx_slot_t *s)
{
    s->type = FX_NONE;
    s->p1 = 64; s->p2 = 64;
    s->mix = 127;
    s->flags = 0;
    s->phase = 0;
    s->svf[0].Init();
    s->svf[1].Init();
    for (uint32_t c = 0; c < 2; c++)
        for (uint32_t i = 0; i < FM1_FX_DELAY; i++) s->dl[c][i] = 0;
}

static inline void fx_rack_init(fx_rack_t *r)
{
    for (uint32_t i = 0; i < FM1_FX_SLOTS; i++) fx_slot_init(&r->slot[i]);
    r->par[0] = r->par[1] = 0;
    r->cursor = 0;
}

static inline void fx_slot_set_type(fx_slot_t *s, uint8_t t)
{
    if (t >= FX_NTYPES || t == s->type) return;
    uint8_t p1 = s->p1, p2 = s->p2, mix = s->mix, flags = s->flags;
    fx_slot_init(s);            /* state across a type change is meaningless */
    s->type = t;
    s->p1 = p1; s->p2 = p2; s->mix = mix; s->flags = flags;
    if (t == FX_LP || t == FX_HP) {
        for (int c = 0; c < 2; c++) {
            s->svf[c].set_mode(t == FX_HP ? braids::SVF_MODE_HP
                                          : braids::SVF_MODE_LP);
            s->svf[c].set_frequency((int16_t)((int32_t)s->p1 << 7));
            s->svf[c].set_resonance((int16_t)((int32_t)s->p2 * 258));
        }
    }
}

static inline void fx_slot_set_p1(fx_slot_t *s, uint8_t v)
{
    s->p1 = v;
    if (s->type == FX_LP || s->type == FX_HP)
        for (int c = 0; c < 2; c++)
            s->svf[c].set_frequency((int16_t)((int32_t)v << 7));
}

static inline void fx_slot_set_p2(fx_slot_t *s, uint8_t v)
{
    s->p2 = v;
    if (s->type == FX_LP || s->type == FX_HP)
        for (int c = 0; c < 2; c++)
            s->svf[c].set_resonance((int16_t)((int32_t)v * 258));
}

/* --- the blocks ----------------------------------------------------------- */

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
 * The period is 2^17, so the wrap is a mask -- no division on a chip whose
 * divides are expensive.
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

static inline void fx_slot_tick(fx_slot_t *s, uint32_t w,
                                int32_t il, int32_t ir,
                                int32_t *ol, int32_t *orr)
{
    int32_t wl = il, wr = ir;

    switch (s->type) {
    case FX_CHORUS: {
        /* Two taps 90 degrees apart, which is what makes it stereo without a
         * second LFO. p1 is rate, p2 is depth. */
        uint32_t inc = 200000u + (uint32_t)s->p1 * 26000u;   /* ~0.05..0.8 Hz */
        s->phase += inc;

        int32_t depth = (int32_t)s->p2;                      /* 0..127 */
        for (int c = 0; c < 2; c++) {
            uint32_t ph = s->phase + (c ? 0x40000000u : 0u); /* +90 degrees */
            uint32_t p16 = ph >> 16;                         /* 0..65535 */
            int32_t  tri = (int32_t)(p16 < 32768u ? p16 : 65535u - p16);

            /* 4..20 ms of delay, modulated. Q8 for a fractional tap. */
            int32_t d_q8 = (180 << 8) + ((tri * depth * 3) >> 8);
            uint32_t di = (uint32_t)(d_q8 >> 8), df = (uint32_t)(d_q8 & 0xFF);
            if (di > FM1_FX_DELAY - 2u) di = FM1_FX_DELAY - 2u;

            uint32_t r0 = (w - di) & FM1_FX_DMASK;
            uint32_t r1 = (r0 - 1u) & FM1_FX_DMASK;
            int32_t a = s->dl[c][r0], b = s->dl[c][r1];
            int32_t v = a + (((b - a) * (int32_t)df) >> 8);
            if (c) wr = v; else wl = v;
        }
        break;
    }
    case FX_FOLD: {
        int32_t drive = Q12_ONE + (int32_t)s->p1 * 24;       /* 1.0 .. ~1.75x */
        int32_t bias  = ((int32_t)s->p2 - 64) * 64;
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
        sd = (sd * (int32_t)s->p1) >> 6;
        wl = m + sd;
        wr = m - sd;
        break;
    }
    case FX_LP: case FX_HP:
        wl = s->svf[0].Process(il);
        wr = s->svf[1].Process(ir);
        break;
    default:
        break;      /* FX_NONE: wet is dry, and the mix below is a no-op */
    }

    /* The delay line is written with the DRY input, always -- so a slot
     * switched on mid-patch reads recent audio rather than silence, or
     * whatever was sitting in it a minute ago. */
    s->dl[0][w] = il;
    s->dl[1][w] = ir;

    if (s->mix >= 127) { *ol = wl; *orr = wr; return; }
    *ol  = il + (((wl - il) * (int32_t)s->mix) >> 7);
    *orr = ir + (((wr - ir) * (int32_t)s->mix) >> 7);
}

/* Run the rack over one interleaved stereo block, in place.
 *
 * The write cursor lives in the rack and advances with the stream. Restarting
 * it per block would reset every delay line every 5.8 ms and turn the chorus
 * into a buzz at 172 Hz. */
static inline void fx_rack_process(fx_rack_t *r, int32_t *bus, uint32_t frames)
{
    /* Gather the active slots ONCE per block rather than testing all four on
     * every sample.
     *
     * The first version tested inside the sample loop, and an empty rack cost
     * 12,816 cycles a block -- 0.9% of the audio budget to decide four times
     * per frame that there was nothing to do. Slot configuration changes at
     * knob rate, so hoisting it is free and exact. */
    fx_slot_t *act[FM1_FX_SLOTS];
    uint32_t n = 0;
    for (uint32_t k = 0; k < FM1_FX_SLOTS; k++)
        if (r->slot[k].type != FX_NONE) act[n++] = &r->slot[k];

    if (n == 0) return;         /* an idle rack must cost nothing at all */

    uint32_t w = r->cursor;

    for (uint32_t i = 0; i < frames; i++) {
        int32_t l = bus[2 * i], rr = bus[2 * i + 1];

        for (uint32_t k = 0; k < n; k++) {
            fx_slot_t *s = act[k];
            int32_t ol, orr;
            if (s->flags & FM1_FX_PAR) {
                /* Same input as the slot above; outputs sum. */
                fx_slot_tick(s, w, r->par[0], r->par[1], &ol, &orr);
                l += ol; rr += orr;
            } else {
                r->par[0] = l; r->par[1] = rr;   /* what a following PAR shares */
                fx_slot_tick(s, w, l, rr, &ol, &orr);
                l = ol; rr = orr;
            }
        }

        bus[2 * i] = l;
        bus[2 * i + 1] = rr;
        w = (w + 1u) & FM1_FX_DMASK;
    }

    r->cursor = w;
}

}  // namespace fm1

#endif /* FM1_RACK_H */
