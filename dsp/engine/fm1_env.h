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

typedef struct {
    int32_t  level;         /* current gain, Q24 */
    int32_t  sustain;       /* sustain level, Q24 */

    uint32_t a_attack;      /* segment rates, Q24, from fm1_env_a[] */
    uint32_t a_decay;
    uint32_t a_release;

    uint32_t hold_len;      /* samples */
    uint32_t held;

    uint8_t  stage;
    uint8_t  gate;
} fm1_env_t;

static inline void fm1_env_init(fm1_env_t *e)
{
    e->level = 0;
    e->sustain = FM1_ENV_ONE;
    e->a_attack = e->a_decay = e->a_release = fm1_env_a[0];
    e->hold_len = 0;
    e->held = 0;
    e->stage = ENV_IDLE;
    e->gate = 0;
}

static inline void fm1_env_set_attack(fm1_env_t *e, uint8_t v)
{ e->a_attack = fm1_env_a[v & 127]; }

static inline void fm1_env_set_decay(fm1_env_t *e, uint8_t v)
{ e->a_decay = fm1_env_a[v & 127]; }

static inline void fm1_env_set_release(fm1_env_t *e, uint8_t v)
{ e->a_release = fm1_env_a[v & 127]; }

static inline void fm1_env_set_sustain(fm1_env_t *e, uint8_t v)
{ e->sustain = ((int32_t)(v & 127) * FM1_ENV_ONE) / 127; }

/* HOLD reuses the rate table rather than carrying a second one.
 *
 * A one-pole's time constant in samples is 2^24 / a, so inverting the
 * coefficient recovers the same geometric 0.4 ms .. 7 s sweep the other
 * segments use -- which means HOLD's knob feels like theirs, for free. The
 * divide is 32-bit and happens on a knob turn, not per sample; only 64-bit
 * division is forbidden (docs/RULES.md). */
static inline void fm1_env_set_hold(fm1_env_t *e, uint8_t v)
{
    e->hold_len = v ? (uint32_t)FM1_ENV_ONE / fm1_env_a[v & 127] : 0u;
}

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
    uint32_t a;

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

    case ENV_ATTACK:  target = FM1_ENV_ONE; a = e->a_attack;  break;
    case ENV_DECAY:   target = e->sustain;  a = e->a_decay;   break;
    default:          target = 0;           a = e->a_release; break;
    }

    /* 64-bit multiply, then a shift. The chip has no 64-bit DIVIDE, which is
     * the rule; multiplication is fine and is what makes Q24 affordable. */
    int32_t delta =
        (int32_t)(((int64_t)(target - e->level) * (int64_t)a) >> FM1_ENV_BITS);

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
