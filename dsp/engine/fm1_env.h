/* SPDX-License-Identifier: MIT
 *
 * The per-voice VCA envelope.
 *
 * Two parameters, ATTACK and RELEASE, each roughly 2 ms to 32 seconds. Not an
 * ADSR: voices here are latched and mostly static, so sustain is the normal
 * state and decay has nothing to decay to. What this instrument actually
 * needs is a drone that can arrive over twenty seconds and leave over thirty,
 * which nothing else in the signal path provides.
 *
 * It also fixes a defect rather than adding a feature. The gate used to be
 * `if (!gate) continue;`, which stops a voice mid-waveform -- a step from
 * whatever the oscillator happened to be outputting straight to zero. That is
 * a click, every time, on every voice.
 *
 * One-pole exponential, from the table in fm1_env_table.h:
 *
 *     env += (target - env) * a
 *
 * Exponential rather than linear because a linear amplitude fade is only
 * -6 dB at its halfway point: almost all of the audible change happens in the
 * last fraction, so it sounds like the note hangs and then drops. This is
 * roughly linear in dB, which is what fading sounds like.
 *
 * Q24 state. Q15 would be too coarse at the slow end -- the coefficient there
 * rounds to 1 in Q15, turning a documented 32 second fade into 3.4 seconds.
 *
 * Plain C; dsp/tests exercises it directly.
 */
#ifndef FM1_ENV_H
#define FM1_ENV_H

#include "../shared/fm1_env_table.h"
#include "../shared/fm1_fixed.h"

#define FM1_ENV_BITS  24
#define FM1_ENV_ONE   (1 << FM1_ENV_BITS)

/* Below this the voice counts as silent and stops being rendered. 1/4096 of
 * full scale is about -72 dB, comfortably under the noise floor of a 16-bit
 * output, so nothing audible is cut off -- but it is a floor rather than
 * zero, because an exponential never actually arrives and a voice rendering
 * forever at -200 dB costs exactly as much as one you can hear. */
#define FM1_ENV_EPS   (FM1_ENV_ONE / 4096)

typedef struct {
    int32_t  level;         /* current gain, Q24 */
    uint32_t a_attack;      /* coefficients, Q24, from fm1_env_a[] */
    uint32_t a_release;
    uint8_t  gate;
} fm1_env_t;

static inline void fm1_env_init(fm1_env_t *e)
{
    e->level = 0;
    e->a_attack  = fm1_env_a[0];
    e->a_release = fm1_env_a[0];
    e->gate = 0;
}

static inline void fm1_env_set_attack(fm1_env_t *e, uint8_t v)
{ e->a_attack = fm1_env_a[v & 127]; }

static inline void fm1_env_set_release(fm1_env_t *e, uint8_t v)
{ e->a_release = fm1_env_a[v & 127]; }

static inline void fm1_env_gate(fm1_env_t *e, uint8_t on)
{ e->gate = on ? 1u : 0u; }

/* Jump straight to the target, for a hard kill that must not ring on.
 * SHIFT+MUTE uses this; MUTE alone closes the gate and lets RELEASE run
 * (docs/SIGNAL_PATH.md, OPEN-2). */
static inline void fm1_env_reset(fm1_env_t *e)
{ e->level = e->gate ? FM1_ENV_ONE : 0; }

/* True when the voice is closed AND has finished fading, so the caller can
 * skip rendering it entirely. A silent voice should cost nothing. */
static inline int fm1_env_idle(const fm1_env_t *e)
{ return !e->gate && e->level < FM1_ENV_EPS; }

/* One sample. Returns the gain in Q24. */
static inline int32_t fm1_env_tick(fm1_env_t *e)
{
    int32_t  target = e->gate ? FM1_ENV_ONE : 0;
    uint32_t a      = e->gate ? e->a_attack : e->a_release;

    /* 64-bit multiply, then a shift. The chip has no 64-bit DIVIDE, which is
     * the rule in docs/RULES.md; multiplication is fine, and it is what makes
     * Q24 affordable here. */
    int32_t delta =
        (int32_t)(((int64_t)(target - e->level) * (int64_t)a) >> FM1_ENV_BITS);

    /* An exponential approaches but never arrives, and in integer arithmetic
     * it stalls outright once the remaining distance times the coefficient
     * rounds to zero. So when the step rounds to nothing, go the rest of the
     * way.
     *
     * The stall distance is 1/a, which is NOT a constant -- it is 18 counts
     * at the fast end and 310,000 at the slow end. A fixed threshold near the
     * target therefore works for fast envelopes and silently leaves slow ones
     * 1.9% short, which is what the first version did. Testing the step
     * rather than the distance adapts to whatever coefficient is in use.
     *
     * The jump it can make is at most 1/a of full scale, and it happens when
     * the signal is already that quiet, so it is far below the waveform's own
     * slew -- the click test holds that. */
    if (delta == 0) e->level = target;
    else            e->level += delta;

    return e->level;
}

#endif /* FM1_ENV_H */
