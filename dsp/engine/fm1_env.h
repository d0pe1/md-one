/* SPDX-License-Identifier: MIT
 *
 * The per-voice VCA envelope: AHDSR.
 *
 *     ATTACK   silence to full
 *     HOLD     stay at full for a while
 *     DECAY    fall to the sustain level
 *     SUSTAIN  stay there as long as the gate is open
 *     RELEASE  fall to silence when it closes
 *
 * HOLD is the one that earns its place on a drone instrument. A long attack
 * into an immediate decay never actually arrives anywhere -- the two curves
 * meet and the peak is a corner you cannot hear. A hold segment gives the top
 * of the swell somewhere to sit, which is the difference between a pad that
 * breathes and one that merely wobbles. It is also what makes AD-with-no-
 * sustain usable as a percussive hit.
 *
 * Every segment is one-pole exponential, from the rate table in
 * fm1_env_table.h. Exponential rather than linear because a linear amplitude
 * fade is only -6 dB at its halfway point: nearly all the audible change
 * happens in the last fraction, so it sounds like the note hangs and then
 * drops. A one-pole approach is roughly linear in dB, which is what fading
 * sounds like.
 *
 * Q24 state. Q15 is too coarse at the slow end -- the coefficient there
 * rounds to 1, turning a documented 32 second fade into 3.4 seconds.
 *
 * This block also exists to stop a click. The gate used to be
 * `if (!gate) continue;`, which stops a voice mid-waveform: a step straight
 * to zero, on every mute, on every voice.
 *
 * Plain C; dsp/tests exercises it directly.
 */
#ifndef FM1_ENV_H
#define FM1_ENV_H

#include "../shared/fm1_env_table.h"
#include "../shared/fm1_fixed.h"

#define FM1_ENV_BITS  24
#define FM1_ENV_ONE   (1 << FM1_ENV_BITS)

enum {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_HOLD,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE,
    ENV_STAGES
};

static const char *const FM1_ENV_STAGE_NAME[ENV_STAGES] = {
    "---", "ATK", "HLD", "DEC", "SUS", "REL"
};

/* What the envelope IS, which on this instrument is mostly a question about
 * the gate rather than about the curve.
 *
 * RELEASE measured correct from the day it was written -- 0.01 s, 0.45 s and
 * 32.11 s at 0, 64 and 127 -- and still appeared to do nothing, because
 * nothing a player does closes the gate. A note-off is ignored by design
 * ("a note is a decision to sound, not a gesture held down"), so MUTE was
 * the only gesture in the instrument that could reach RELEASE. The knob was
 * not broken; it was unreachable.
 *
 *   DRONE    a note latches. Note-off is ignored, as before, and RELEASE is
 *            reached only by MUTE. This is the instrument's own idea of
 *            itself and stays the default.
 *   ANALOG   note-off releases. Exponential segments, roughly linear in dB,
 *            which is what a fade sounds like.
 *   DIGITAL  note-off releases. Straight-line segments -- it ARRIVES rather
 *            than approaches, which is a sampler's envelope rather than an
 *            RC curve.
 *
 * The two are matched by ear, not by stopwatch. At RELEASE 100 the straight
 * line reaches silence in 4.05 s while the exponential takes 5.83 s, but the
 * exponential spends the last 3.8 s of that below a tenth of full scale,
 * inaudible. Measured against the -20 dB point instead -- 2.03 s -- the two
 * fall over the same stretch of time, which is the comparison a player
 * makes. make -C dsp envshape prints both.
 *
 * Only DIGITAL changes anything in here. DRONE and ANALOG differ purely in
 * what the firmware does with a key release, so the engine does not need to
 * tell them apart. */
enum {
    ENV_M_DRONE = 0,
    ENV_M_ANALOG,
    ENV_M_DIGITAL,
    ENV_M_NMODES
};

static const char *const FM1_ENVMODE_NAME[ENV_M_NMODES] = {
    "DRONE", "ANALOG", "DIGITAL"
};

typedef struct {
    int32_t  level;         /* current gain, Q24 */
    int32_t  sustain;       /* sustain level, Q24 */

    uint32_t a_attack;      /* segment rates, Q24, from fm1_env_a[] */
    uint32_t a_decay;
    uint32_t a_release;

    /* The same three segments as STRAIGHT LINES, for ENV_M_DIGITAL. Kept
     * beside the rates rather than derived in the sample loop: they change
     * only when a knob moves, and a 64-bit multiply per sample to rediscover
     * a constant is the block-rate lesson this project has relearned four
     * times. */
    uint32_t l_attack, l_decay, l_release;

    uint32_t hold_len;      /* samples */
    uint32_t held;

    uint8_t  stage;
    uint8_t  gate;
    uint8_t  mode;          /* ENV_M_* */
} fm1_env_t;

/* The straight-line step that covers the same ground in the same time.
 *
 * A one-pole settles in about 4.6 time constants and its time constant is
 * 2^24 / a samples, so a segment lasts 4.6 * 2^24 / a samples. A line
 * crossing full scale in that many samples advances a / 4.6 each sample;
 * 28494 / 2^17 is 1 / 4.6.
 *
 * Never zero while the rate is not, or the segment would stall for ever at
 * the slow end -- the exponential path has the snap below to finish it, a
 * line has nothing. */
static inline uint32_t fm1_env_lin(uint32_t a)
{
    uint32_t l = (uint32_t)(((uint64_t)a * 28494u) >> 17);
    return (a && !l) ? 1u : l;
}

static inline void fm1_env_init(fm1_env_t *e)
{
    e->level = 0;
    e->sustain = FM1_ENV_ONE;
    e->a_attack = e->a_decay = e->a_release = fm1_env_a[0];
    e->l_attack = e->l_decay = e->l_release = fm1_env_lin(fm1_env_a[0]);
    e->hold_len = 0;
    e->held = 0;
    e->stage = ENV_IDLE;
    e->gate = 0;
    e->mode = ENV_M_DRONE;
}

static inline void fm1_env_set_mode(fm1_env_t *e, uint8_t m)
{ e->mode = m < ENV_M_NMODES ? m : ENV_M_DRONE; }

/* The rate for a Q8 knob position -- the coarse byte plus its fraction.
 *
 * Fine editing only exists where the ENGINE reads the fraction. These four
 * times were added to Drone::HasFine without this, so SHIFT offered a
 * sub-step on a control that still quantised to the byte: a gesture with no
 * effect, which is the one failure this project keeps paying for. The test
 * missed it because it asserted the LIST rather than the behaviour.
 *
 * Geometric interpolation would be the exact thing -- the table is
 * exponential -- but linear between adjacent entries is within 0.4% of it
 * at this spacing, and 0.4% of a time constant is not audible. */
static inline uint32_t fm1_env_rate_q8(int32_t q8)
{
    int32_t i, f;
    uint32_t a, b;

    if (q8 < 0) q8 = 0;
    if (q8 > 127 << 8) q8 = 127 << 8;
    i = q8 >> 8;
    f = q8 & 255;
    if (i >= 127 || !f) return fm1_env_a[i];
    a = fm1_env_a[i];
    b = fm1_env_a[i + 1];
    /* The table FALLS as the knob rises, so this interpolates downward. */
    return a - (uint32_t)(((uint64_t)(a - b) * (uint32_t)f) >> 8);
}

static inline void fm1_env_set_attack_q8(fm1_env_t *e, int32_t q8)
{ e->a_attack = fm1_env_rate_q8(q8); e->l_attack = fm1_env_lin(e->a_attack); }
static inline void fm1_env_set_attack(fm1_env_t *e, uint8_t v)
{ fm1_env_set_attack_q8(e, (int32_t)(v & 127) << 8); }

static inline void fm1_env_set_decay_q8(fm1_env_t *e, int32_t q8)
{ e->a_decay = fm1_env_rate_q8(q8); e->l_decay = fm1_env_lin(e->a_decay); }
static inline void fm1_env_set_decay(fm1_env_t *e, uint8_t v)
{ fm1_env_set_decay_q8(e, (int32_t)(v & 127) << 8); }

static inline void fm1_env_set_release_q8(fm1_env_t *e, int32_t q8)
{ e->a_release = fm1_env_rate_q8(q8); e->l_release = fm1_env_lin(e->a_release); }
static inline void fm1_env_set_release(fm1_env_t *e, uint8_t v)
{ fm1_env_set_release_q8(e, (int32_t)(v & 127) << 8); }

static inline void fm1_env_set_sustain(fm1_env_t *e, uint8_t v)
{ e->sustain = ((int32_t)(v & 127) * FM1_ENV_ONE) / 127; }

/* HOLD reuses the rate table rather than carrying a second one.
 *
 * A one-pole's time constant in samples is 2^24 / a, so inverting the
 * coefficient recovers the same geometric 0.4 ms .. 7 s sweep the other
 * segments use -- which means HOLD's knob feels like theirs, for free. The
 * divide is 32-bit and happens on a knob turn, not per sample; only 64-bit
 * division is forbidden (docs/RULES.md). */
static inline void fm1_env_set_hold_q8(fm1_env_t *e, int32_t q8)
{
    uint32_t a = fm1_env_rate_q8(q8);
    e->hold_len = (q8 >= 256) ? (uint32_t)FM1_ENV_ONE / a : 0u;
}
static inline void fm1_env_set_hold(fm1_env_t *e, uint8_t v)
{ fm1_env_set_hold_q8(e, (int32_t)(v & 127) << 8); }

static inline void fm1_env_gate(fm1_env_t *e, uint8_t on)
{
    if (on) {
        /* Retrigger from wherever it is, rather than from zero: restarting
         * the attack from silence on a voice that is already sounding is a
         * click, which is the thing this block exists to avoid. */
        e->stage = ENV_ATTACK;
        e->held = 0;
        e->gate = 1;
    } else {
        if (e->stage != ENV_IDLE) e->stage = ENV_RELEASE;
        e->gate = 0;
    }
}

/* Jump straight to the end state, for a hard kill that must not ring on.
 * SHIFT+MUTE uses this; MUTE alone closes the gate and lets RELEASE run
 * (docs/SIGNAL_PATH.md, OPEN-2). */
static inline void fm1_env_reset(fm1_env_t *e)
{
    if (e->gate) { e->level = FM1_ENV_ONE; e->stage = ENV_SUSTAIN; }
    else         { e->level = 0;           e->stage = ENV_IDLE; }
    e->held = 0;
}

/* True when the voice contributes nothing and can be skipped entirely.
 *
 * Level alone is not enough: an attack starts at zero and must not be
 * skipped before it has risen. A sustain of zero IS skippable, which is what
 * makes an AD percussive patch cost nothing once it has decayed. */
static inline int fm1_env_idle(const fm1_env_t *e)
{
    return e->level == 0 && e->stage != ENV_ATTACK;
}

/* One sample. Returns the gain in Q24. */
static inline int32_t fm1_env_tick(fm1_env_t *e)
{
    int32_t target;
    uint32_t a, lin;

    switch (e->stage) {
    case ENV_HOLD:
        /* Flat at the top. The only segment that is a counter rather than a
         * curve, and the reason a slow swell has an audible peak instead of
         * a corner where two curves meet. */
        if (++e->held >= e->hold_len) e->stage = ENV_DECAY;
        return e->level;

    case ENV_SUSTAIN:
        /* Re-read every sample so moving the SUSTAIN knob while a voice is
         * held does something, instead of waiting for the next note. */
        e->level = e->sustain;
        return e->level;

    case ENV_IDLE:
        return 0;

    case ENV_ATTACK:  target = FM1_ENV_ONE; a = e->a_attack;  lin = e->l_attack;  break;
    case ENV_DECAY:   target = e->sustain;  a = e->a_decay;   lin = e->l_decay;   break;
    default:          target = 0;           a = e->a_release; lin = e->l_release; break;
    }

    /* 64-bit multiply, then a shift. The chip has no 64-bit DIVIDE, which is
     * the rule; multiplication is fine and is what makes Q24 affordable.
     *
     * TAKEN ON THE MAGNITUDE AND SIGNED BACK, because `>>` on a signed
     * product is an arithmetic shift: it FLOORS, it does not divide. Rising
     * segments were fine -- a positive product truncates toward zero and the
     * snap below fires. Falling ones never could: floor of any value between
     * -1 and 0 is -1, never 0, so DECAY and RELEASE could not reach the
     * `delta == 0` test at all and ended only by arriving one LSB per sample.
     *
     * At RELEASE 127 that meant the exponential finished at 25 s with the
     * level still at 1.85% of full, and then crawled LINEARLY for another 7
     * seconds to reach zero -- in a file whose whole premise is that the
     * segments are exponential. Worse, fm1_env_idle() needs level == 0, so
     * the voice was not skipped for any of it: about 59 million cycles of
     * full voice render, a third of a second of it below -60 dBFS.
     *
     * This is the FOURTH time this project has shipped a floored negative
     * shift -- detune, the comb's tuning, the resonator's, and now this. */
    int32_t diff = target - e->level;
    int32_t mag  = diff < 0 ? -diff : diff;
    int32_t step;
    int32_t delta;

    if (e->mode == ENV_M_DIGITAL) {
        /* A straight line ARRIVES. Clamped to what is left so it lands
         * exactly on the target rather than stepping past it and sawing
         * back, and so the snap below still fires to end the segment. */
        step = (int32_t)lin;
        if (step > mag) step = mag;
    } else {
        step = (int32_t)(((int64_t)mag * (int64_t)a) >> FM1_ENV_BITS);
    }
    delta = diff < 0 ? -step : step;

    /* An exponential approaches but never arrives, and in integer arithmetic
     * it stalls outright once the remaining distance times the coefficient
     * rounds to zero. So when the step rounds to nothing, go the rest of the
     * way -- and that is also what ends the segment.
     *
     * The stall distance is 1/a, which is NOT a constant: 18 counts at the
     * fast end, 310,000 at the slow end. A fixed threshold near the target
     * therefore works for fast envelopes and silently leaves slow ones 1.9%
     * short, which is exactly what the first version did. Testing the STEP
     * rather than the distance adapts to whatever rate is loaded. */
    if (delta == 0) {
        e->level = target;
        switch (e->stage) {
        case ENV_ATTACK:
            e->held = 0;
            e->stage = e->hold_len ? ENV_HOLD : ENV_DECAY;
            break;
        case ENV_DECAY:   e->stage = ENV_SUSTAIN; break;
        default:          e->stage = ENV_IDLE;    break;   /* release */
        }
    } else {
        e->level += delta;
    }

    return e->level;
}

#endif /* FM1_ENV_H */
