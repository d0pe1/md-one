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
static_assert(FD_P_EQ_BASE    == P_EQ_BASE,     "EQ base mirror drifted");
static_assert(FD_P_EQ_STRIDE  == P_EQ_STRIDE,   "EQ stride mirror drifted");
static_assert(FD_P_EQ(2, FD_P_EQ_MIX) == P_EQ(2, P_EQ_MIX), "EQ index mirror drifted");
static_assert(FD_EQ_BANDS     == EQ_BANDS,      "EQ band count mirror drifted");
static_assert(FD_EQB_PASS     == EQB_PASS,      "EQ mode mirror drifted");
static_assert(FD_EQB_NOTCH    == EQB_NOTCH,     "EQ mode mirror drifted");
static_assert(FD_P_DRIVE      == P_DRIVE,       "param mirror drifted");
static_assert(FD_P_ATTACK     == P_ATTACK,      "param mirror drifted");
static_assert(FD_P_HOLD       == P_HOLD,        "param mirror drifted");
static_assert(FD_P_DECAY      == P_DECAY,       "param mirror drifted");
static_assert(FD_P_SUSTAIN    == P_SUSTAIN,     "param mirror drifted");
static_assert(FD_P_RELEASE    == P_RELEASE,     "param mirror drifted");
static_assert(FD_P_LEVEL      == P_LEVEL,       "param mirror drifted");
static_assert(FD_P_PAN        == P_PAN,         "param mirror drifted");
static_assert(FD_P_MONO       == P_MONO,        "param mirror drifted");
static_assert(FD_P_DEST       == P_DEST,        "param mirror drifted");
static_assert(FD_NPARAM       == NPARAM,        "param mirror drifted");
static_assert(FD_NGPARAM      == NGPARAM,       "global mirror drifted");
static_assert(FD_G_FX_BASE    == G_FX_BASE,     "patchbay base mirror drifted");
static_assert(FD_G_FX_STRIDE  == G_FX_STRIDE,   "patchbay stride mirror drifted");
static_assert(FD_G_FX(2, FD_G_FX_MIX) == G_FX(2, G_FX_MIX), "patchbay index mirror drifted");
static_assert(FD_G_FX_DEST    == G_FX_DEST,     "patchbay dest mirror drifted");
static_assert(FD_DEST_OFF     == DEST_OFF,      "dest mirror drifted");
static_assert(FD_DEST_FX1     == DEST_FX1,      "dest mirror drifted");
static_assert(FD_DEST_OUT     == DEST_OUT,      "dest mirror drifted");
static_assert(FD_DEST_NDEST   == DEST_NDEST,    "dest mirror drifted");
static_assert(FD_FX_CHORUS    == FX_CHORUS,     "effect type mirror drifted");
static_assert(FD_FX_NTYPES    == FX_NTYPES,     "effect type mirror drifted");
static_assert(FD_VOICES       == FM1_VOICES,    "voice count mirror drifted");
static_assert(FD_ENV_ATTACK   == ENV_ATTACK,    "envelope stage mirror drifted");
static_assert(FD_ENV_HOLD     == ENV_HOLD,      "envelope stage mirror drifted");
static_assert(FD_ENV_RELEASE  == ENV_RELEASE,   "envelope stage mirror drifted");
static_assert(FD_ENV_STAGES   == ENV_STAGES,    "envelope stage mirror drifted");
static_assert(FD_G_LFO_BASE   == G_LFO_BASE,    "LFO base mirror drifted");
static_assert(FD_G_LFO_STRIDE == G_LFO_STRIDE,  "LFO stride mirror drifted");
static_assert(FD_G_LFO(3, FD_G_LFO_MODE) == G_LFO(3, G_LFO_MODE), "LFO index mirror drifted");
static_assert(FD_LFOS         == FM1_LFOS,      "LFO count mirror drifted");
static_assert(FD_LFO_SMOOTH   == LFO_SMOOTH,    "LFO mode mirror drifted");
static_assert(FD_LFO_NMODES   == LFO_NMODES,    "LFO mode mirror drifted");
static_assert(FD_P_MD_BASE    == P_MD_BASE,     "mod matrix base mirror drifted");
static_assert(FD_P_MD_STRIDE  == P_MD_STRIDE,   "mod matrix stride mirror drifted");
static_assert(FD_P_MD(8, FD_P_MD_SRC) == P_MD(8, P_MD_SRC), "mod index mirror drifted");
static_assert(FD_MD_NDEST     == MD_NDEST,      "mod destination mirror drifted");
static_assert(FD_MD_PAN       == MD_PAN,        "mod destination mirror drifted");
static_assert(FD_MD_SRC_NONE  == MD_SRC_NONE,   "mod source mirror drifted");
static_assert(FD_MD_SRC_DRIFT == MD_SRC_DRIFT,  "mod source mirror drifted");

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
uint8_t fm1_drone_get_global(uint8_t p)                     { return g_drone.GetGlobal(p); }
void fm1_drone_snap_params(void)                            { g_drone.SnapParams(); }
uint8_t fm1_drone_env_stage(uint8_t v)                      { return g_drone.EnvStage(v); }
int16_t fm1_drone_lfo_value(uint8_t n)                      { return g_drone.LfoValue(n); }

uint8_t fm1_drone_meter(uint8_t v)    { return g_drone.Meter(v); }
uint8_t fm1_drone_clipping(uint8_t v) { return g_drone.Clipping(v) ? 1u : 0u; }

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
