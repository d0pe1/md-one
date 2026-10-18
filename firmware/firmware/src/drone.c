/* SPDX-License-Identifier: GPL-3.0-only
 *
 * MD-One: a drone machine for the M-VAVE FM-1.
 *
 * Four always-on voices, each a Mutable Instruments Braids macro oscillator
 * through two multimode filters, an EQ and a drive, into a four-channel FX
 * patchbay and a mixer. The engine is in dsp/engine (MIT); the control
 * surface is ui.c; this file is the instrument -- what the keys do, what
 * survives a power cut, and how MIDI gets in.
 *
 * The panel is documented in docs/UX_DESIGN.md and carried by a printed
 * overlay; the stock silkscreen underneath says something else and is not a
 * constraint. ui.c holds the mapping.
 *
 *   keys        set the Subject voice's note and latch it on
 *   SHIFT+key   set the note WITHOUT unmuting -- audition routing silently
 *   SELECT      the Subject: voice, FX channel or EQ band, per page
 *   ALGORITHM   the page's enum; SHIFT jumps whole shape categories
 *   KNOB1..4    the page's four parameters
 *   OCT- OCT+   the Subject voice's octave, in every menu
 *   MUTE        mute/unmute the Subject voice; SHIFT solos it
 *   HIT         strike every voice; SHIFT strikes the Subject only
 *   SHIFT+SAVE  write the patch now
 *   MASTER      the volume (platform.c)
 *
 *   USB-MIDI    channels 1-4 are the voices, channel 16 is global
 *               (docs/MIDI_SPEC.md)
 *   USB audio   the FM-1 is a stereo input: record it straight into a DAW
 *
 * A note-off never stops a voice. This is a drone machine: a note is a
 * decision to sound, not a gesture held down.
 *
 * Built on Dinghy by zvenson, whose platform is Felucca's (Leo Kuroshita,
 * Hugelton Instruments) as SLOOP carries it. Rules of the road: integers
 * only, no float, no 64-bit division (docs/RULES.md).
 */
#include "platform.c"

#include "fm1_drone_c.h"

/* ---- the sound (the audio ISR: short, no flash, no screen) */

static void app_audio(int32_t *out, uint32_t n)
{
    fm1_drone_render_add(out, n);
}

/* ---- state (main loop) */

#define NV FD_VOICES

#define SOLO_NONE 0xFFu
#define KEY0      53u                  /* key 0 = F3, as Dinghy */

static struct {
    uint8_t note[NV];
    uint8_t on[NV];                    /* the user's mute state */
    uint8_t solo;                      /* voice index, or SOLO_NONE */
} st;

/* ui.c draws the latched note on the HOME page. It is the only thing the UI
 * needs from the instrument that is not a parameter, which is why it is a
 * function rather than a shared struct. */
static uint8_t ui_note_of(uint8_t v) { return v < NV ? st.note[v] : 0u; }

#include "ui.c"

/* A voice sounds if it is unmuted and nothing else is soloed. Applied with
 * SetGate so mute and solo never re-strike -- un-soloing a bowed voice should
 * bring it back as it was, not re-bow it. */
static void apply_gates(void)
{
    uint8_t v;
    for (v = 0; v < NV; v++) {
        uint8_t audible = st.on[v] && (st.solo == SOLO_NONE || st.solo == v);
        fm1_drone_set_gate(v, audible);
    }
}

/* What survives a power-off: the whole patch.
 *
 * The globals are in here now. Without them the FX patchbay -- which is where
 * much of a patch's character lives -- came back empty after a power cycle
 * while every voice parameter survived, which would read as a bug long before
 * anyone thought to blame the save format. 4 x 42 + 25 + 9 = 202 bytes, and
 * the settings sector is 256, so it fits with room to spare. One sector is
 * still one patch (docs/UX_DESIGN.md section 10.4). */
typedef struct {
    uint8_t magic;
    uint8_t note[NV];
    uint8_t on[NV];
    uint8_t p[NV][FD_NPARAM];
    uint8_t g[FD_NGPARAM];
} patch_t;

/* Bumped from 0xD1 when the globals were added and the per-voice octave moved
 * into P_OCTAVE: an old patch has a different layout, and loading it would
 * scatter its bytes across the wrong parameters. */
#define PATCH_MAGIC 0xD2

static void patch_store(patch_t *q)
{
    uint8_t v, i;
    q->magic = PATCH_MAGIC;
    for (v = 0; v < NV; v++) {
        q->note[v] = st.note[v];
        q->on[v] = st.on[v];
        for (i = 0; i < FD_NPARAM; i++) q->p[v][i] = fm1_drone_get_param(v, i);
    }
    for (i = 0; i < FD_NGPARAM; i++) q->g[i] = fm1_drone_get_global(i);
}

static void patch_apply(const patch_t *q)
{
    uint8_t v, i;
    for (i = 0; i < FD_NGPARAM; i++) fm1_drone_set_global(i, q->g[i]);
    for (v = 0; v < NV; v++) {
        for (i = 0; i < FD_NPARAM; i++) fm1_drone_set_param(v, i, q->p[v][i]);
        st.note[v] = q->note[v];
        st.on[v] = q->on[v];
        fm1_drone_note_on(v, st.note[v]);       /* pitch, then gate below */
    }
    apply_gates();
}

/* ---- the keyboard strip */

static int black_key(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static void draw_keys(uint32_t lit)
{
    uint32_t k, w = 0;
    cv_begin(240, 28, C_BLACK);
    for (k = 0; k < 27u; k++)
        if (!black_key(KEY0 + k)) {
            cv_rect(8 + (int32_t)w * 14, 0, 13, 28,
                    lit >> k & 1u ? C_ACC : RGB(210, 210, 210));
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)
        if (black_key(KEY0 + k))
            cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 17,
                    lit >> k & 1u ? C_ACC : C_BLACK);
        else
            w++;
    cv_blit(0, Y_KEYS);
}

/* ---- main */

static void app_main(void)
{
    patch_t saved;
    uint32_t prev = 0, dirty = 0, t_change = 0, t_meter = 0;
    uint32_t lit_drawn = ~0u, redraw = 1;
    uint8_t v;

    fm1_drone_init();
    fm1_nav_init(&ui_nav);

    /* A sounding default, so the instrument makes noise the moment it boots:
     * four detuned voices an octave apart with a little comb and drift. */
    for (v = 0; v < NV; v++) {
        st.note[v] = (uint8_t)(29 + v * 7);
        st.on[v] = 1;
        fm1_drone_set_param(v, FD_P_SHAPE,       (uint8_t)(v == 3 ? 12 : 9));
        fm1_drone_set_param(v, FD_P_TIMBRE,      (uint8_t)(70 - v * 8));
        fm1_drone_set_param(v, FD_P_DETUNE,      (uint8_t)(64 + (v & 1 ? 3 : -3)));
        fm1_drone_set_param(v, FD_P_DRIFT_RATE,  (uint8_t)(10 + v * 4));
        fm1_drone_set_param(v, FD_P_FM,          (uint8_t)(40 + v * 6));
        /* F1 in comb mode is the chain this boot patch has always used; the
         * comb became a filter mode rather than a block of its own. */
        fm1_drone_set_param(v, FD_P_F1_TYPE,     FD_F_COMB);
        fm1_drone_set_param(v, FD_P_F1_RES,      (uint8_t)(40 + v * 14));
        fm1_drone_set_param(v, FD_P_F1_MIX,      (uint8_t)(28 + v * 10));
        fm1_drone_set_param(v, FD_P_LEVEL,       (uint8_t)(104 - v * 12));
        fm1_drone_set_param(v, FD_P_PAN,         (uint8_t)(26 + v * 26));
        fm1_drone_note_on(v, st.note[v]);
    }
    /* st lives in .bss, so this would otherwise default to 0 -- which is a
     * valid voice index, and would boot with voice 1 soloed. */
    st.solo = SOLO_NONE;
    apply_gates();

    settings_load(&saved, sizeof saved);
    if (saved.magic == PATCH_MAGIC) patch_apply(&saved);

    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, b, rel, lit = 0;
        uint8_t stt, d1, d2;

        platform_poll();

        /* SHIFT is read as a HELD state, not an edge. It has no function of
         * its own on any page -- invariant 4 -- so it is never consumed. */
        ui_shift = (fm1_in.buttons >> UI_B_SHIFT) & 1u;

        /* --- keys: set the Subject voice's note and latch it on ---------- */
        keys = fm1_in.notes;
        down = keys & ~prev;
        prev = keys;
        if (down) {
            uint32_t k;
            for (k = 0; k < 27u; k++)
                if (down >> k & 1u) {
                    uint8_t sv = ui_voice();
                    st.note[sv] = (uint8_t)(KEY0 + k);
                    /* SHIFT + key sets the note WITHOUT unmuting, so routing
                     * can be auditioned silently. */
                    if (!ui_shift) st.on[sv] = 1;
                    fm1_drone_note_on(sv, st.note[sv]);
                    apply_gates();
                    midi_send(0x90u + sv, st.note[sv], 100);
                    redraw = 1; dirty = 1; t_change = fm1_ms;
                    break;
                }
        }

        /* --- buttons -----------------------------------------------------
         * A menu button enters its menu at page 1 and cycles from there;
         * SHIFT returns to page 1. One button reaches every page of its own
         * menu, so there is no separate page control to find. */
        b = fm1_input_edges(&rel);

        if (b & (1u << UI_B_EDIT)) { fm1_nav_press(&ui_nav, M_EDIT, (int)ui_shift); redraw = 1; }
        if (b & (1u << UI_B_ENV))  { fm1_nav_press(&ui_nav, M_ENV,  (int)ui_shift); redraw = 1; }
        if (b & (1u << UI_B_FX))   { fm1_nav_press(&ui_nav, M_FX,   (int)ui_shift); redraw = 1; }
        if (b & (1u << UI_B_MIX))  { fm1_nav_press(&ui_nav, M_MIX,  (int)ui_shift); redraw = 1; }
        if (b & (1u << UI_B_GLO))  { fm1_nav_press(&ui_nav, M_GLO,  (int)ui_shift); redraw = 1; }
        if (b & (1u << UI_B_HOME)) { fm1_nav_press(&ui_nav, M_HOME, (int)ui_shift); redraw = 1; }

        /* SAVE enters the patch menu; SHIFT + SAVE writes immediately rather
         * than waiting out the idle timer below. */
        if (b & (1u << UI_B_SAVE)) {
            if (ui_shift) {
                patch_store(&saved);
                settings_save(&saved, sizeof saved);
                dirty = 0;
            } else {
                fm1_nav_press(&ui_nav, M_SAVE, 0);
            }
            redraw = 1;
        }

        /* MUTE closes the VCA; SHIFT solos.
         *
         * NOTE: docs/SIGNAL_PATH.md section 7 (OPEN-2) instead gives
         * SHIFT+MUTE as a hard kill that also clears filter and rack state,
         * while UX_DESIGN section 4 says solo. Solo wins here -- UX_DESIGN is
         * the control-surface authority, and solo is the one reached for
         * while playing. The hard kill has no button and is logged as an open
         * item rather than silently dropped. */
        if (b & (1u << UI_B_MUTE)) {
            uint8_t sv = ui_voice();
            if (ui_shift) st.solo = (st.solo == sv) ? SOLO_NONE : sv;
            else          st.on[sv] = (uint8_t)!st.on[sv];
            apply_gates();
            redraw = 1; dirty = 1; t_change = fm1_ms;
        }

        /* HIT re-excites: the plucked and struck shapes are silent without
         * it. SHIFT strikes the Subject alone. */
        if (b & (1u << UI_B_HIT)) {
            if (ui_shift) fm1_drone_strike(ui_voice());
            else for (v = 0; v < NV; v++) fm1_drone_strike(v);
        }

        /* OCT moves the Subject voice's OCTAVE PARAMETER, in every menu.
         * Not a duplicate of the PITCH page's KNOB1 but a faster path to the
         * same place: the parameter lives in the table once, so it has one CC
         * and one patch byte however it is reached. */
        if (b & (1u << BTN_OCT_DOWN)) {
            uint8_t o = fm1_drone_get_param(ui_voice(), FD_P_OCTAVE);
            if (o > 0) fm1_drone_set_param(ui_voice(), FD_P_OCTAVE, (uint8_t)(o - 1u));
            redraw = 1; dirty = 1; t_change = fm1_ms;
        }
        if (b & (1u << BTN_OCT_UP)) {
            uint8_t o = fm1_drone_get_param(ui_voice(), FD_P_OCTAVE);
            if (o < FM1_PDEF[P_OCTAVE].max)
                fm1_drone_set_param(ui_voice(), FD_P_OCTAVE, (uint8_t)(o + 1u));
            redraw = 1; dirty = 1; t_change = fm1_ms;
        }

        /* --- encoders ---------------------------------------------------- */
        if (ui_encoders()) { redraw = 1; dirty = 1; t_change = fm1_ms; }

        /* --- USB-MIDI -----------------------------------------------------
         * Channels 1-4 are the voices, channel 16 is global
         * (docs/MIDI_SPEC.md). */
        while (midi_get(&stt, &d1, &d2)) {
            uint8_t ch = stt & 0x0Fu, msg = stt & 0xF0u;
            if (msg == 0xB0u && d1 == 123u) {
                for (v = 0; v < NV; v++) st.on[v] = 0;
                st.solo = SOLO_NONE;
                apply_gates();
                redraw = 1;
            } else if (msg == 0xB0u) {
                fm1_drone_midi_cc(ch, d1, d2);
                redraw = 1;
            } else if (msg == 0x90u && d2 && ch < NV) {
                st.note[ch] = d1;
                st.on[ch] = 1;
                fm1_drone_note_on(ch, d1);
                apply_gates();
                redraw = 1;
            } else if ((msg == 0x80u || msg == 0x90u) && ch < NV) {
                /* A note-off does NOT silence a drone voice: holding a key is
                 * not how this instrument is played. Use MUTE or CC 123. */
            }
        }

        /* --- LEDs and screen ---------------------------------------------- */
        for (v = 0; v < NV; v++) {
            int32_t rel_k = (int32_t)st.note[v] - (int32_t)KEY0;
            if (st.on[v] && rel_k >= 0 && rel_k < 27) lit |= 1u << rel_k;
        }
        {
            uint32_t k;
            for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
        }
        ui_leds();
        fm1_led_key(UI_B_MUTE, st.on[ui_voice()]);
        /* Lit while the voice can still go that way, dark at the limits --
         * the panel teaching the same clamping SELECT uses. */
        fm1_led_key(BTN_OCT_DOWN,
                    fm1_drone_get_param(ui_voice(), FD_P_OCTAVE) > 0);
        fm1_led_key(BTN_OCT_UP,
                    fm1_drone_get_param(ui_voice(), FD_P_OCTAVE) < FM1_PDEF[P_OCTAVE].max);

        if (lit != lit_drawn) { lit_drawn = lit; draw_keys(lit); }
        if (redraw)           { redraw = 0; ui_draw(); }

        /* The meters have to move on their own -- nothing presses a button to
         * make a drone get louder. 20 Hz is fast enough to read a peak and
         * slow enough to leave the audio alone. */
        if (ui_nav.menu == M_MIX && fm1_ms - t_meter > 50u) {
            t_meter = fm1_ms;
            ui_draw_body();
        }

        /* A flash erase stalls everything including the audio, so only save
         * when the change has settled. A drone never goes quiet, so unlike
         * Dinghy we cannot wait for silence -- 4 s of no edits instead. */
        if (dirty && fm1_ms - t_change > 4000u) {
            patch_store(&saved);
            settings_save(&saved, sizeof saved);
            dirty = 0;
        }
    }
}
