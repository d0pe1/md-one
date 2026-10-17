/* SPDX-License-Identifier: MIT */
#include "fm1_drone_c.h"

#include "fm1_drone.h"

/* The C header mirrors the parameter indices; keep them honest. */
static_assert(FD_P_SHAPE      == P_SHAPE,       "param mirror drifted");
static_assert(FD_P_TIMBRE     == P_TIMBRE,      "param mirror drifted");
static_assert(FD_P_COLOR      == P_COLOR,       "param mirror drifted");
static_assert(FD_P_FM         == P_FM,          "param mirror drifted");
static_assert(FD_P_MOD        == P_MOD,         "param mirror drifted");
static_assert(FD_P_DRIFT_RATE == P_DRIFT_RATE,  "param mirror drifted");
static_assert(FD_P_OCTAVE     == P_OCTAVE,      "param mirror drifted");
static_assert(FD_P_DETUNE     == P_DETUNE,      "param mirror drifted");
static_assert(FD_P_GLIDE      == P_GLIDE,       "param mirror drifted");
static_assert(FD_P_F1_TYPE    == P_F1_TYPE,     "param mirror drifted");
static_assert(FD_P_F1_CUT     == P_F1_CUT,      "param mirror drifted");
static_assert(FD_P_F1_RES     == P_F1_RES,      "param mirror drifted");
static_assert(FD_P_F1_MIX     == P_F1_MIX,      "param mirror drifted");
static_assert(FD_P_F2_TYPE    == P_F2_TYPE,     "param mirror drifted");
static_assert(FD_P_F2_CUT     == P_F2_CUT,      "param mirror drifted");
static_assert(FD_P_F2_RES     == P_F2_RES,      "param mirror drifted");
static_assert(FD_P_F2_MIX     == P_F2_MIX,      "param mirror drifted");
static_assert(FD_F_OFF        == F_OFF,         "filter mode mirror drifted");
static_assert(FD_F_COMB       == F_COMB,        "filter mode mirror drifted");
static_assert(FD_F_RESO       == F_RESO,        "filter mode mirror drifted");
static_assert(FD_F_NMODES     == F_NMODES,      "filter mode mirror drifted");
static_assert(FD_P_LEVEL      == P_LEVEL,       "param mirror drifted");
static_assert(FD_P_PAN        == P_PAN,         "param mirror drifted");
static_assert(FD_NPARAM       == NPARAM,        "param mirror drifted");
static_assert(FD_NGPARAM      == NGPARAM,       "global mirror drifted");
static_assert(FD_VOICES       == FM1_VOICES,    "voice count mirror drifted");

/* The engine's state is about 87 KB -- four MacroOscillators at 17 KB each
 * plus the combs. The device's .data/.bss region is 96 KiB and the platform
 * already uses 12 KB of it, so this must live in .pool: a separate 336 KiB
 * region the linker script provides for exactly this (big zero-initialised
 * buffers). It is zeroed at boot and Init() sets everything anyway.
 *
 * FM1_POOL is empty on the host, where it is just a static. */
#if defined(__pi32v2__) || defined(FM1_TARGET)
#define FM1_POOL __attribute__((section(".pool")))
#else
#define FM1_POOL
#endif

static FM1_POOL fm1::Drone g_drone;
static FM1_POOL int16_t    g_mix[FM1_BLOCK * 2];
static fm1_ccmap_t         g_cc;        /* 256 bytes: .bss is fine */

extern "C" {

void fm1_drone_init(void)
{
    g_drone.Init();
    fm1_ccmap_build(&g_cc);
}

void fm1_drone_note_on(uint8_t v, uint8_t n) { g_drone.NoteOn(v, n); }
void fm1_drone_note_off(uint8_t v)           { g_drone.NoteOff(v); }
void fm1_drone_strike(uint8_t v)             { g_drone.Strike(v); }
void fm1_drone_set_gate(uint8_t v, uint8_t on) { g_drone.SetGate(v, on); }

void fm1_drone_set_param(uint8_t v, uint8_t p, uint8_t val) { g_drone.SetParam(v, p, val); }
uint8_t fm1_drone_get_param(uint8_t v, uint8_t p)           { return g_drone.GetParam(v, p); }
void fm1_drone_set_global(uint8_t p, uint8_t val)           { g_drone.SetGlobal(p, val); }

const char *fm1_drone_param_name(uint8_t p)
{
    return (p < NPARAM && FM1_PDEF[p].name) ? FM1_PDEF[p].name : "?";
}

int fm1_drone_midi_cc(uint8_t channel, uint8_t cc, uint8_t value)
{
    if (cc > 127) return 0;

    if (channel < FM1_VOICES) {
        uint8_t p = g_cc.voice[cc];
        if (!p) return 0;
        g_drone.SetParam(channel, (uint8_t)(p - 1), value);
        return 1;
    }
    if (channel == 15) {
        uint8_t p = g_cc.global[cc];
        if (!p) return 0;
        g_drone.SetGlobal((uint8_t)(p - 1), value);
        return 1;
    }
    return 0;
}

void fm1_drone_render_add(int32_t *out, uint32_t n)
{
    while (n) {
        uint32_t chunk = (n > FM1_BLOCK) ? FM1_BLOCK : n;

        for (uint32_t i = 0; i < chunk * 2u; i++) g_mix[i] = 0;
        g_drone.Render(g_mix, chunk);
        for (uint32_t i = 0; i < chunk * 2u; i++) out[i] += g_mix[i];

        out += chunk * 2u;
        n -= chunk;
    }
}

}  // extern "C"
