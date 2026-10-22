/* SPDX-License-Identifier: MIT
 *
 * A Chamberlin state-variable filter that hands back ALL of its outputs.
 *
 * braids::Svf is already in the tree and is a fine filter, but Process()
 * returns only the one output its mode selects, and the state advances when
 * you call it -- so you cannot ask it for lowpass and bandpass at the same
 * sample. An EQ band needs exactly that: a shelf is the input plus a scaled
 * lowpass, a bell is the input plus a scaled bandpass, a pass is one output
 * used alone. One filter, four taps, chosen per band.
 *
 * It is also the only structure here with a directly tunable Q, which the
 * per-band RESONANCE control needs and which braids::Svf's
 * resonance-through-a-damping-table does not expose.
 *
 * The vendored file is NOT modified: it is pinned by hash in
 * dsp/mi/PROVENANCE.md so it can be re-pulled, and fm1_filter.h and
 * fm1_rack.h keep using it unchanged.
 *
 * The algorithm is the standard Chamberlin digital SVF (Hal Chamberlin,
 * "Musical Applications of Microprocessors"), written from the published
 * difference equations rather than adapted from anyone's source, so this file
 * is MIT and dsp/ stays free of GPL (docs/LICENSE_REGISTER.md):
 *
 *     lp += f * bp
 *     hp  = in - lp - q * bp
 *     bp += f * hp
 *     notch = hp + lp
 *
 * Plain C: nothing from Braids, so dsp/tests can use it directly.
 */
#ifndef FM1_SVF_H
#define FM1_SVF_H

#include "../shared/fm1_fixed.h"
#include "../shared/fm1_svf_table.h"

typedef struct {
    int32_t lp, bp;         /* state */
    int32_t f;              /* tuning,  Q15, from fm1_svf_f[] */
    int32_t q;              /* damping, Q15: this is 1/Q, so SMALL is resonant */
} fm1_svf_t;

/* Every output of one sample, so a caller can build a shelf, a bell or a pass
 * from the same pass through the filter. */
typedef struct {
    int32_t lp, bp, hp, notch;
} fm1_svf_out_t;

static inline void fm1_svf_init(fm1_svf_t *s)
{
    s->lp = s->bp = 0;
    s->f  = fm1_svf_f[60];
    s->q  = 32768 / 2;      /* 1/Q = 0.5, i.e. Q = 2: a broad, usable bell */
}

/* Tune by MIDI note, like every other frequency control on the instrument. */
static inline void fm1_svf_set_note(fm1_svf_t *s, uint8_t note)
{
    s->f = fm1_svf_f[note & 127];
}

/* Tune from a pitch in 1/128 SEMITONE -- the unit the oscillator, the comb
 * and the voice key all speak -- INTERPOLATING between table entries.
 *
 * fm1_svf_f[] is indexed by whole MIDI note, so anything that reached it by
 * shifting a pitch down (`key >> 7`) landed on a semitone grid and stayed
 * there until the pitch crossed a boundary. On the resonator slot that had
 * exactly the fault the comb turned out to have: a note sitting on the grid
 * is N*128, one cent flat is N*128 - 1, and the shift floors that to N - 1 --
 * so the first click below centre dropped the resonator a whole semitone
 * while fifty cents sharp moved it not at all.
 *
 * Linear between adjacent entries is ample. A semitone is 5.9% and the
 * coefficient is near-linear across one, leaving error far below the 0.78
 * cent step of the pitch unit feeding it. */
static inline int32_t fm1_svf_cut_q15(int32_t pitch)
{
    int32_t n, frac, a, b;

    if (pitch < 0)            pitch = 0;
    if (pitch > 127 * 128)    pitch = 127 * 128;
    n    = pitch >> 7;
    frac = pitch & 127;
    a = fm1_svf_f[n];
    /* The top note has nothing above it to reach toward, and no fraction
     * left either once the clamp above has run. */
    b = n < 127 ? fm1_svf_f[n + 1] : a;
    return a + (((b - a) * frac) >> 7);
}

static inline void fm1_svf_set_pitch(fm1_svf_t *s, int32_t pitch)
{
    s->f = fm1_svf_cut_q15(pitch);
}

/* Resonance from a 0..127 knob. 0 is heavily damped, 127 is nearly ringing.
 *
 * The map is to 1/Q, not Q, because that is what the difference equation
 * wants and because the stability condition is on this value: f + q < 2. q is
 * floored well above zero -- at exactly zero the filter is an oscillator that
 * never decays, which is a lovely sound and a terrible thing to find waiting
 * at the end of a knob. */
static inline void fm1_svf_set_res(fm1_svf_t *s, uint8_t res)
{
    /* 1/Q from about 1.4 (res 0, damped) down to 0.04 (res 127, sharp). */
    int32_t q = 45875 - ((int32_t)res * 44565) / 127;
    if (q < 1300) q = 1300;
    s->q = q;
}

static inline fm1_svf_out_t fm1_svf_tick(fm1_svf_t *s, int32_t in)
{
    fm1_svf_out_t o;

    s->lp += (s->f * s->bp) >> Q15_BITS;
    /* The integrators are clamped, not left to wrap. A resonant filter driven
     * hard is the likeliest place for an overflow in the whole instrument,
     * and wrapping turns a loud ring into broadband noise -- which the comb
     * taught once already (fm1_comb.h). */
    if (s->lp >  Q15_ONE) s->lp =  Q15_ONE;
    if (s->lp < -Q15_ONE) s->lp = -Q15_ONE;

    o.hp = in - s->lp - ((s->q * s->bp) >> Q15_BITS);
    if (o.hp >  Q15_ONE) o.hp =  Q15_ONE;
    if (o.hp < -Q15_ONE) o.hp = -Q15_ONE;

    s->bp += (s->f * o.hp) >> Q15_BITS;
    if (s->bp >  Q15_ONE) s->bp =  Q15_ONE;
    if (s->bp < -Q15_ONE) s->bp = -Q15_ONE;

    o.lp = s->lp;
    o.bp = s->bp;
    o.notch = o.hp + o.lp;
    return o;
}

#endif /* FM1_SVF_H */
