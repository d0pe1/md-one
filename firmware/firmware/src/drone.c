/* SPDX-License-Identifier: GPL-3.0-only
 *
 * A drone machine for the M-VAVE FM-1.
 *
 * Four always-on voices, each a Mutable Instruments Braids macro oscillator
 * through a tuned comb, with slow independent drift. The engine itself is in
 * dsp/engine (MIT); this file is the instrument: panel, screen and MIDI.
 *
 *   keys        set the note of the selected voice and latch it on
 *   SEL         select the next voice
 *   HOME        SOLO the selected voice: hear one on its own
 *   PLAY        mute/unmute the selected voice
 *   REC         re-strike every voice (the plucked and struck shapes need it)
 *   OCT- OCT+   transpose the selected voice
 *   ALGORITHM   the Braids shape of the selected voice (the module's EDIT)
 *   KNOB1..4    TIMBRE, COLOR, FM, MOD -- the module's own four knobs
 *   with EDIT   LEVEL, COMB TUNE, COMB FEEDBACK, COMB MIX
 *   PRESETS     the selected voice's level, so balancing is always one knob
 *
 * The module has FINE and COARSE as well; they are dropped here because the
 * keys and OCTAVE already set pitch. FM and MODULATION are attenuverters for
 * CV inputs on the module. The FM-1 has no CV jacks, so they scale the
 * internal drift into pitch and timbre instead -- same layout, same meaning,
 * no jacks. Both are bipolar with centre = no modulation.
 *   MASTER      the volume (platform.c)
 *
 *   USB-MIDI    channels 1-4 are the voices: notes play them, CC edits them
 *               (docs/MIDI_SPEC.md); channel 16 is global
 *   USB audio   the FM-1 is a stereo input: record it straight into a DAW
 *
 * Built on Dinghy by zvenson, whose platform is Felucca's (Leo Kuroshita,
 * Hugelton Instruments) as SLOOP carries it. Rules of the road: integers only,
 * no float, no 64-bit division (docs/RULES.md).
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

static struct {
    uint8_t note[NV];
    uint8_t on[NV];                /* the user's mute state */
    uint8_t solo;                  /* voice index, or SOLO_NONE */
    uint8_t sel;
    int8_t  oct[NV];
} st;

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

/* What survives a power-off: the whole patch, which is just the parameter
 * table's values plus our note assignments. settings is 256 bytes; 4 voices x
 * 13 parameters + bookkeeping fits comfortably. */
typedef struct {
    uint8_t magic;
    uint8_t note[NV];
    uint8_t on[NV];
    int8_t  oct[NV];
    uint8_t p[NV][FD_NPARAM];
} patch_t;
#define PATCH_MAGIC 0xD1

static void patch_store(patch_t *q)
{
    uint8_t v, i;
    q->magic = PATCH_MAGIC;
    for (v = 0; v < NV; v++) {
        q->note[v] = st.note[v];
        q->on[v] = st.on[v];
        q->oct[v] = st.oct[v];
        for (i = 0; i < FD_NPARAM; i++) q->p[v][i] = fm1_drone_get_param(v, i);
    }
}

static void patch_apply(const patch_t *q)
{
    uint8_t v, i;
    for (v = 0; v < NV; v++) {
        for (i = 0; i < FD_NPARAM; i++) fm1_drone_set_param(v, i, q->p[v][i]);
        st.note[v] = q->note[v];
        st.on[v] = q->on[v];
        st.oct[v] = q->oct[v];
        fm1_drone_note_on(v, st.note[v]);       /* pitch, then gate below */
    }
    apply_gates();
}

/* ---- the screen */

#define C_ACC  RGB(180, 140, 255)
#define C_GREY  RGB(90, 90, 90)
#define C_ON   RGB(120, 230, 170)
#define KEY0   53u                     /* key 0 = F3, as Dinghy */

static int black_key(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static const char *const NOTE_NAME[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static void note_text(uint8_t n, char *s)
{
    const char *p = NOTE_NAME[n % 12u];
    uint32_t i = 0;
    while (*p) s[i++] = *p++;
    if (n >= 12u) s[i++] = (char)('0' + n / 12u - 1u);
    s[i] = 0;
}

static void u8_text(uint8_t v, char *s)
{
    uint32_t i = 0;
    if (v >= 100u) s[i++] = (char)('0' + v / 100u);
    if (v >= 10u)  s[i++] = (char)('0' + (v / 10u) % 10u);
    s[i++] = (char)('0' + v % 10u);
    s[i] = 0;
}

static void app_str(char *line, uint32_t *i, const char *p)
{
    while (*p) line[(*i)++] = *p++;
}

/* One row per voice: number, state, note, shape, level.
 * The level is the thing you reach for when balancing, so it is on screen
 * rather than hidden behind a knob you have to turn to discover. */
static void draw_voices(void)
{
    uint8_t v;
    for (v = 0; v < NV; v++) {
        char line[28], nb[8], sb[8], lb[8];
        uint32_t i = 0;
        uint8_t audible = st.on[v] && (st.solo == SOLO_NONE || st.solo == v);

        note_text(st.note[v], nb);
        u8_text(fm1_drone_get_param(v, FD_P_SHAPE), sb);
        u8_text(fm1_drone_get_param(v, FD_P_LEVEL), lb);

        line[i++] = (char)('1' + v);
        line[i++] = (st.solo == v) ? 'S' : (st.on[v] ? '*' : '.');
        line[i++] = ' ';
        app_str(line, &i, nb);
        while (i < 7u) line[i++] = ' ';
        app_str(line, &i, "SH");
        app_str(line, &i, sb);
        while (i < 14u) line[i++] = ' ';
        app_str(line, &i, "L");
        app_str(line, &i, lb);
        line[i] = 0;

        lcd_fill(0, (int32_t)(96u + v * 22u), 240, 20, C_BLACK);
        draw_text_box(0, (int32_t)(96u + v * 22u), 240, &FONT_S, line,
                      v == st.sel ? C_ACC : (audible ? C_ON : C_GREY), 1);
    }
}

/* The four knob values, with the bank's own labels above them, so what the
 * knobs do is always visible instead of remembered. */
static void draw_params(uint32_t edit)
{
    char line[32], a[8], b[8], c[8], d[8];
    uint32_t i = 0;

    if (edit) {
        u8_text(fm1_drone_get_param(st.sel, FD_P_DETUNE), a);
        u8_text(fm1_drone_get_param(st.sel, FD_P_DRIFT_RATE), b);
        u8_text(fm1_drone_get_param(st.sel, FD_P_DRIFT_DEPTH), c);
        u8_text(fm1_drone_get_param(st.sel, FD_P_COMB_FB), d);
    } else {
        u8_text(fm1_drone_get_param(st.sel, FD_P_LEVEL), a);
        u8_text(fm1_drone_get_param(st.sel, FD_P_TIMBRE), b);
        u8_text(fm1_drone_get_param(st.sel, FD_P_COLOR), c);
        u8_text(fm1_drone_get_param(st.sel, FD_P_COMB_MIX), d);
    }

    lcd_fill(0, 70, 240, 18, C_BLACK);
    draw_text_box(0, 70, 240, &FONT_S,
                  edit ? "DETU DRFT DPTH CMFB" : "LEVL TIMB COLR CMIX",
                  edit ? C_ACC : C_GREY, 1);

    app_str(line, &i, a); line[i++] = ' ';
    app_str(line, &i, b); line[i++] = ' ';
    app_str(line, &i, c); line[i++] = ' ';
    app_str(line, &i, d);
    line[i] = 0;

    lcd_fill(0, 188, 240, 18, C_BLACK);
    draw_text_box(0, 188, 240, &FONT_S, line, C_WHITE, 1);
}

static void draw_keys(uint32_t lit)
{
    uint32_t k, w = 0;
    cv_begin(240, 28, C_BLACK);
    for (k = 0; k < 27u; k++)
        if (!black_key(KEY0 + k)) {
            cv_rect(8 + (int32_t)w * 14, 0, 13, 28, lit >> k & 1u ? C_ACC : RGB(210, 210, 210));
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)
        if (black_key(KEY0 + k))
            cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 17, lit >> k & 1u ? C_ACC : C_BLACK);
        else
            w++;
    cv_blit(0, 208);
}

/* ---- parameter nudging from an encoder, clamped */

/* STEP is per detent. The first version used 2, which needed 64 detents --
 * nearly three full turns -- to cross a 0..127 range, so the knobs felt
 * dead. 6 crosses it in about one turn, which is what a knob should do. */
#define STEP 6

static void nudge(uint8_t v, uint8_t param, int32_t delta, uint8_t max)
{
    int32_t x = (int32_t)fm1_drone_get_param(v, param) + delta;
    if (x < 0) x = 0;
    if (x > (int32_t)max) x = (int32_t)max;
    fm1_drone_set_param(v, param, (uint8_t)x);
}

/* ---- main */

static void app_main(void)
{
    patch_t saved;
    uint32_t prev = 0, dirty = 0, t_change = 0;
    uint32_t lit_drawn = ~0u, rows_dirty = 1, params_dirty = 1;
    uint32_t edit_held = 0, edit_drawn = ~0u;
    uint8_t v;

    fm1_drone_init();

    /* A sounding default, so the instrument makes noise the moment it boots:
     * four detuned voices an octave apart with a little comb and drift. */
    for (v = 0; v < NV; v++) {
        st.note[v] = (uint8_t)(29 + v * 7);
        st.on[v] = 1;
        st.oct[v] = 0;
        fm1_drone_set_param(v, FD_P_SHAPE,       (uint8_t)(v == 3 ? 12 : 9));
        fm1_drone_set_param(v, FD_P_TIMBRE,      (uint8_t)(70 - v * 8));
        fm1_drone_set_param(v, FD_P_DETUNE,      (uint8_t)(64 + (v & 1 ? 3 : -3)));
        fm1_drone_set_param(v, FD_P_DRIFT_RATE,  (uint8_t)(10 + v * 4));
        fm1_drone_set_param(v, FD_P_DRIFT_DEPTH, (uint8_t)(40 + v * 6));
        fm1_drone_set_param(v, FD_P_COMB_FB,     (uint8_t)(40 + v * 14));
        fm1_drone_set_param(v, FD_P_COMB_MIX,    (uint8_t)(28 + v * 10));
        fm1_drone_set_param(v, FD_P_LEVEL,       (uint8_t)(104 - v * 12));
        fm1_drone_set_param(v, FD_P_PAN,         (uint8_t)(26 + v * 26));
        fm1_drone_note_on(v, st.note[v]);
    }
    st.sel = 0;
    /* st lives in .bss, so this would otherwise default to 0 -- which is a
     * valid voice index, and would boot with voice 1 soloed. */
    st.solo = SOLO_NONE;
    apply_gates();

    settings_load(&saved, sizeof saved);
    if (saved.magic == PATCH_MAGIC) patch_apply(&saved);

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    draw_text_box(0, 52, 240, &FONT_S, "four drones  v" APP_VER, C_GREY, 1);
    /* the knob labels are drawn by draw_params(), which follows the bank */

    for (;;) {
        uint32_t keys, down, b, rel, lit = 0;
        int32_t e;
        uint8_t stt, d1, d2;

        platform_poll();

        /* --- keys: set the selected voice's note and latch it on --------- */
        keys = fm1_in.notes;
        down = keys & ~prev;
        prev = keys;
        if (down) {
            uint32_t k;
            for (k = 0; k < 27u; k++)
                if (down >> k & 1u) {
                    int32_t n = (int32_t)(KEY0 + k) + 12 * st.oct[st.sel];
                    if (n < 0) n = 0;
                    if (n > 127) n = 127;
                    st.note[st.sel] = (uint8_t)n;
                    st.on[st.sel] = 1;
                    fm1_drone_note_on(st.sel, st.note[st.sel]);
                    midi_send(0x90u + st.sel, st.note[st.sel], 100);
                    rows_dirty = 1;
                    dirty = 1; t_change = fm1_ms;
                    break;
                }
        }

        /* --- buttons ----------------------------------------------------- */
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_SEL)) {
            st.sel = (uint8_t)((st.sel + 1u) % NV);
            rows_dirty = params_dirty = 1;
        }
        if (b & (1u << BTN_PLAY)) {
            st.on[st.sel] = (uint8_t)!st.on[st.sel];
            apply_gates();
            rows_dirty = 1; dirty = 1; t_change = fm1_ms;
        }
        if (b & (1u << BTN_HOME)) {              /* SOLO: hear one voice alone */
            st.solo = (st.solo == st.sel) ? SOLO_NONE : st.sel;
            apply_gates();
            rows_dirty = 1;
        }
        if (b & (1u << BTN_REC))
            for (v = 0; v < NV; v++) fm1_drone_strike(v);
        if (b & (1u << BTN_OCT_DOWN) && st.oct[st.sel] > -3) {
            st.oct[st.sel]--;
            st.note[st.sel] = (uint8_t)(st.note[st.sel] - 12u);
            fm1_drone_note_on(st.sel, st.note[st.sel]);
            rows_dirty = 1; dirty = 1; t_change = fm1_ms;
        }
        if (b & (1u << BTN_OCT_UP) && st.oct[st.sel] < 3) {
            st.oct[st.sel]++;
            st.note[st.sel] = (uint8_t)(st.note[st.sel] + 12u);
            fm1_drone_note_on(st.sel, st.note[st.sel]);
            rows_dirty = 1; dirty = 1; t_change = fm1_ms;
        }

        /* --- encoders ----------------------------------------------------- */
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
            int32_t s = (int32_t)st.sel + e;
            while (s < 0) s += NV;
            st.sel = (uint8_t)(s % NV);
            rows_dirty = params_dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            nudge(st.sel, FD_P_SHAPE, e, 47);
            rows_dirty = 1; dirty = 1; t_change = fm1_ms;
        }
        /* Two banks on the same four knobs: holding EDIT swaps them. Doubles
         * the reachable parameters without a mode you can get lost in -- let
         * go and you are back. The labels on screen follow. */
        edit_held = (fm1_in.buttons >> BTN_EDIT) & 1u;
        if (edit_held != edit_drawn) { edit_drawn = edit_held; params_dirty = 1; }

        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            nudge(st.sel, edit_held ? FD_P_DETUNE : FD_P_LEVEL, e * STEP, 127);
            params_dirty = rows_dirty = 1; dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {
            nudge(st.sel, edit_held ? FD_P_DRIFT_RATE : FD_P_TIMBRE, e * STEP, 127);
            params_dirty = 1; dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) {
            nudge(st.sel, edit_held ? FD_P_DRIFT_DEPTH : FD_P_COLOR, e * STEP, 127);
            params_dirty = 1; dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) {
            nudge(st.sel, edit_held ? FD_P_COMB_FB : FD_P_COMB_MIX, e * STEP, 127);
            params_dirty = 1; dirty = 1; t_change = fm1_ms;
        }
        /* Level again, so balancing never needs the right bank. */
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            nudge(st.sel, FD_P_LEVEL, e * STEP, 127);
            params_dirty = rows_dirty = 1; dirty = 1; t_change = fm1_ms;
        }

        /* --- USB-MIDI ------------------------------------------------------
         * Channels 1-4 are the voices, channel 16 is global
         * (docs/MIDI_SPEC.md). This is the part Dinghy did not have: it
         * parsed only note on/off. */
        while (midi_get(&stt, &d1, &d2)) {
            uint8_t ch = stt & 0x0Fu, msg = stt & 0xF0u;
            if (msg == 0xB0u) {
                fm1_drone_midi_cc(ch, d1, d2);
                if (ch == st.sel) params_dirty = 1;
                if (ch < NV) rows_dirty = 1;
            } else if (msg == 0x90u && d2 && ch < NV) {
                st.note[ch] = d1;
                st.on[ch] = 1;
                fm1_drone_note_on(ch, d1);
                rows_dirty = 1;
            } else if ((msg == 0x80u || msg == 0x90u) && ch < NV) {
                /* A note-off does NOT silence a drone voice: holding a key is
                 * not how this instrument is played. Use PLAY or CC 123. */
            } else if (msg == 0xB0u && d1 == 123u) {
                for (v = 0; v < NV; v++) st.on[v] = 0;
                st.solo = SOLO_NONE;
                apply_gates();
                rows_dirty = 1;
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
        fm1_led_key(BTN_SEL, 1);
        fm1_led_key(BTN_PLAY, st.on[st.sel]);
        fm1_led_key(BTN_HOME, st.solo != SOLO_NONE);
        fm1_led_key(BTN_EDIT, edit_held);

        if (lit != lit_drawn)  { lit_drawn = lit; draw_keys(lit); }
        if (rows_dirty)        { rows_dirty = 0; draw_voices(); }
        if (params_dirty)      { params_dirty = 0; draw_params(edit_held); }

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
