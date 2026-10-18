/* SPDX-License-Identifier: GPL-3.0-only
 *
 * The control surface: menus, pages, and what the screen says.
 *
 * The MAP is dsp/engine/fm1_pages.h (MIT, host-tested). This file is the half
 * that cannot be tested on a host -- drawing, and the panel. Keeping them
 * apart is what lets the half with the rules in it be checked by `make test`
 * rather than by looking at a photograph of a screen.
 *
 * Everything here obeys docs/UX_DESIGN.md section 3:
 *
 *   - four knobs, four parameters, and a page with fewer shows `----`
 *   - a button does the same thing in every menu
 *   - SELECT only ever chooses the Subject
 *   - SHIFT is only ever SHIFT
 *   - a menu button enters at page 1 and cycles from there
 *   - the screen always says where you are
 *
 * Included by drone.c after platform.c: the app is a unity build (build.py's
 * APP_C), so this is a .c file that is never compiled on its own.
 */

#include "fm1_pages.h"
#include "fm1_shapes.h"
#include "fm1_fmode.h"
#include "fm1_eq.h"
#include "fm1_route.h"
#include "fm1_env.h"

/* The latched note of a voice. Owned by drone.c, which is the instrument;
 * this file only draws it. Declared here because a unity build compiles
 * ui.c first. */
static uint8_t ui_note_of(uint8_t voice);

/* --- the overlay ---------------------------------------------------------
 *
 * MD-One's button map is NOT the stock silkscreen. This firmware replaces
 * M-VAVE's entirely and ships with a printed overlay, so the only thing that
 * matters is that a button means the same thing everywhere.
 *
 *   overlay:   EDIT  SHIFT ENV   LFO   FX    GLO
 *              HOME  SAVE  MIX   MOD   MUTE  HIT
 *   stock:     FX    SEL   ENV   LFO   EDIT  GLO
 *              HOME  SAVE  ARP   SEQ   PLAY  REC
 *
 * Note that EDIT and FX SWAP POSITIONS -- deliberately, so the two most-used
 * menus sit at the outer edges where the thumbs land. That is exactly the
 * detail that would otherwise be rediscovered as a bug, so the names below
 * are the OVERLAY's and the ids are the panel's.
 */
#define UI_B_EDIT   BTN_FX          /* top 1 */
#define UI_B_SHIFT  BTN_SEL         /* top 2 -- the modifier, nothing else */
#define UI_B_ENV    BTN_ENV         /* top 3 */
#define UI_B_LFO    BTN_LFO         /* top 4 -- Phase 2 */
#define UI_B_FX     BTN_EDIT        /* top 5 */
#define UI_B_GLO    BTN_GLO         /* top 6 */
#define UI_B_HOME   BTN_HOME        /* bot 1 */
#define UI_B_SAVE   BTN_SAVE        /* bot 2 */
#define UI_B_MIX    BTN_ARP         /* bot 3 */
#define UI_B_MOD    BTN_SEQ         /* bot 4 -- Phase 2 */
#define UI_B_MUTE   BTN_PLAY        /* bot 5 */
#define UI_B_HIT    BTN_REC         /* bot 6 */

/* --- colours -------------------------------------------------------------- */
#define C_ACC    RGB(180, 140, 255)     /* the selected thing */
#define C_GREY   RGB(90, 90, 90)        /* labels */
#define C_ON     RGB(120, 230, 170)
#define C_MID    RGB(140, 140, 140)
#define C_RED    RGB(255, 60, 60)
#define C_GREEN  RGB(80, 220, 120)
#define C_BLUE   RGB(90, 160, 255)
#define C_TRACK  RGB(50, 50, 50)        /* unlit segment, inactive knob */

/* --- zones (docs/UX_DESIGN.md section 6) ---------------------------------- */
#define Y_TITLE    2
#define Y_SUBJECT  22
#define Y_RULE     42
#define Y_LABEL    48
#define Y_VALUE    66
#define Y_BODY     90          /* 118 px of page content */
#define Y_KEYS     208
#define COL_W      60          /* 240 / 4 knobs */

#define UI_VOICES  FD_VOICES

/* --- small text helpers ---------------------------------------------------
 * There is no stdio here. These are the whole of our string formatting. */

static void ui_str(char *s, uint32_t *i, const char *p)
{
    while (*p) s[(*i)++] = *p++;
}

/* Zero-padded to three digits, so a column of values does not change width as
 * it crosses 100 -- a number that shifts sideways reads as a number that is
 * doing more than it is. */
static void ui_u8(uint8_t v, char *s)
{
    s[0] = (char)('0' + v / 100u);
    s[1] = (char)('0' + (v / 10u) % 10u);
    s[2] = (char)('0' + v % 10u);
    s[3] = 0;
}

static void ui_i16(int32_t v, char *s)
{
    uint32_t i = 0;
    uint32_t m = (uint32_t)(v < 0 ? -v : v);
    s[i++] = (char)(v < 0 ? '-' : '+');
    if (m >= 100u) s[i++] = (char)('0' + m / 100u);
    s[i++] = (char)('0' + (m / 10u) % 10u);
    s[i++] = (char)('0' + m % 10u);
    s[i] = 0;
}

static const char *const UI_NOTE[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static void ui_note_text(uint8_t n, char *s)
{
    const char *p = UI_NOTE[n % 12u];
    uint32_t i = 0;
    while (*p) s[i++] = *p++;
    if (n >= 12u) s[i++] = (char)('0' + n / 12u - 1u);
    s[i] = 0;
}

/* --- state ---------------------------------------------------------------- */

static fm1_nav_t ui_nav;
static uint32_t  ui_shift;          /* SHIFT held right now */

/* Which voice the keys, the OCT buttons and MUTE act on: the Subject of the
 * page you are looking at -- except on pages whose Subject is something else
 * (an FX channel, an EQ band), where it stays the last voice you chose. That
 * is the whole reason the selectors are per KIND rather than one cursor. */
static uint8_t ui_voice(void) { return ui_nav.sel[SUBJ_VOICE]; }

/* --- reading and writing a parameter --------------------------------------
 *
 * One pair of functions for both scopes, so every page edits the same way and
 * no caller has to know whether it is looking at a voice or a global. */

static uint8_t ui_get(int id)
{
    if (id == PG_NONE) return 0;
    if (FM1_MENU[ui_nav.menu].scope == SCOPE_GLOBAL)
        return fm1_drone_get_global((uint8_t)id);
    return fm1_drone_get_param(ui_voice(), (uint8_t)id);
}

static void ui_set(int id, uint8_t val)
{
    if (id == PG_NONE) return;
    if (FM1_MENU[ui_nav.menu].scope == SCOPE_GLOBAL)
        fm1_drone_set_global((uint8_t)id, val);
    else
        fm1_drone_set_param(ui_voice(), (uint8_t)id, val);
}

/* STEP is per detent, inherited from the first firmware: 6 crosses 0..127 in
 * about one turn, which is what a knob should do. Enums step by ONE -- a
 * filter mode that jumps six positions per detent is unusable, and the whole
 * mode list is six long. */
#define UI_STEP 6

static void ui_nudge(int id, int32_t delta)
{
    const fm1_pdef_t *d = fm1_page_def(&ui_nav, id);
    int32_t x;

    if (!d) return;
    x = (int32_t)ui_get(id) + delta * (d->max > 16 ? UI_STEP : 1);
    if (x < 0) x = 0;
    if (x > (int32_t)d->max) x = (int32_t)d->max;
    ui_set(id, (uint8_t)x);
}

/* --- formatting a value ---------------------------------------------------
 *
 * The kind comes from fm1_params.h, so the screen, the MIDI CC and the patch
 * byte are driven by one declaration. A parameter cannot be shown one way and
 * automated another.
 */
static void ui_fmt(int id, char *out)
{
    const fm1_pdef_t *d = fm1_page_def(&ui_nav, id);
    uint32_t i = 0;
    uint8_t v;

    out[0] = 0;
    if (!d) { ui_str(out, &i, "--"); out[i] = 0; return; }
    v = ui_get(id);

    switch (d->kind) {
    case K_BIP:
    case K_CENTS:
        ui_i16((int32_t)v - 64, out);
        break;
    case K_ONOFF:
        ui_str(out, &i, v ? "ON" : "OFF");
        out[i] = 0;
        break;
    case K_PCT:
        ui_u8((uint8_t)(((uint32_t)v * 100u) / 127u), out);
        out[3] = '%';
        out[4] = 0;
        break;
    case K_FTYPE:
        ui_str(out, &i, FM1_FMODE_NAME[v % F_NMODES]);
        out[i] = 0;
        break;
    case K_FXTYPE:
        ui_str(out, &i, FM1_FXNAME[v % FX_NTYPES]);
        out[i] = 0;
        break;
    case K_DEST:
        ui_str(out, &i, FM1_DESTNAME[v % DEST_NDEST]);
        out[i] = 0;
        break;
    case K_EQMODE:
        ui_str(out, &i,
               FM1_EQ_MODE_NAME[ui_nav.sel[SUBJ_BAND] % EQ_BANDS][v & 1u]);
        out[i] = 0;
        break;
    case K_SHAPE:
        /* The number here, the NAME in the body: 15 characters do not fit a
         * 60 px column, and the body has room for a list. */
        ui_u8(v, out);
        break;
    default:
        ui_u8(v, out);
        break;
    }
}

/* The label over a knob. fm1_label_fit() trims it to the column width; that
 * rule lives in fm1_pages.h so it can be tested, because the overlap it
 * prevents was first found by looking at a photograph of a screen. Usually the parameter's own name, but the two filter
 * pages relabel CUT and RES per mode: CUT is a delay ratio in COMB and a
 * semitone offset in RESO, and a knob called CUTOFF that sets a delay ratio
 * is a knob nobody trusts. */
static const char *ui_label(int id)
{
    const fm1_pdef_t *d = fm1_page_def(&ui_nav, id);
    uint8_t mode;

    if (!d) return "----";

    if (id == P_F1_CUT || id == P_F2_CUT) {
        mode = fm1_drone_get_param(ui_voice(),
                   (uint8_t)(id == P_F1_CUT ? P_F1_TYPE : P_F2_TYPE));
        return FM1_FMODE_CUT_LABEL[mode % F_NMODES];
    }
    if (id == P_F1_RES || id == P_F2_RES) {
        mode = fm1_drone_get_param(ui_voice(),
                   (uint8_t)(id == P_F1_RES ? P_F1_TYPE : P_F2_TYPE));
        return FM1_FMODE_RES_LABEL[mode % F_NMODES];
    }
    return fm1_label_fit(d->name);
}

/* --- the fixed zones ------------------------------------------------------ */

static void ui_draw_header(void)
{
    const fm1_menu_t *m = fm1_nav_menu(&ui_nav);
    const fm1_page_t *p = fm1_nav_page(&ui_nav);
    char line[48];
    uint32_t i = 0;

    ui_str(line, &i, m->name);
    ui_str(line, &i, " - ");
    ui_str(line, &i, p->name);
    while (i < 22u) line[i++] = ' ';
    line[i++] = (char)('1' + ui_nav.page);
    line[i++] = '/';
    line[i++] = (char)('0' + m->npages);
    line[i] = 0;
    draw_text_box(4, Y_TITLE, 236, &FONT_S, line, C_ACC, 0);

    /* The Subject. Blank where a menu has none, rather than a stale one --
     * GLO acting on "VOICE 2" would be a lie. */
    i = 0;
    line[0] = 0;
    if (p->subject != SUBJ_NONE) {
        ui_str(line, &i, FM1_SUBJ_NAME[p->subject]);
        line[i++] = ' ';
        line[i++] = (char)('1' + ui_nav.sel[p->subject]);
        /* On the EQ page both matter: which band you are editing, and whose
         * EQ it is. Showing one without the other invites editing the wrong
         * voice's bottom end for a minute before noticing. */
        if (p->subject == SUBJ_BAND) {
            static const char *const BAND[EQ_BANDS] = { "LOW", "MID", "HIGH" };
            line[i++] = ' ';
            ui_str(line, &i, BAND[ui_nav.sel[SUBJ_BAND] % EQ_BANDS]);
            ui_str(line, &i, "   VOICE ");
            line[i++] = (char)('1' + ui_voice());
        }
        line[i] = 0;
    }
    draw_text_box(4, Y_SUBJECT, 236, &FONT_S, line, C_MID, 0);
    lcd_fill(0, Y_RULE, 240, 1, C_GREY);
}

/* Four labels over four values, each physically under its knob. */
static void ui_draw_knobs(void)
{
    uint8_t k;

    for (k = 0; k < FM1_KNOBS; k++) {
        int  id = fm1_page_knob(&ui_nav, k);
        char val[20];
        int32_t x = (int32_t)k * COL_W;

        ui_fmt(id, val);
        draw_text_box(x, Y_LABEL, COL_W, &FONT_S,
                      id == PG_NONE ? "----" : ui_label(id),
                      id == PG_NONE ? C_TRACK : C_GREY, 1);
        draw_text_box(x, Y_VALUE, COL_W, &FONT_S,
                      id == PG_NONE ? "" : val,
                      id == PG_NONE ? C_TRACK : C_WHITE, 1);
    }
}

/* --- page bodies ----------------------------------------------------------- */

static void ui_body_clear(void)
{
    lcd_fill(0, Y_BODY, 240, Y_KEYS - Y_BODY, C_BLACK);
}

/* The shape list. 48 on a bare encoder would be unusable, so the body spends
 * its space on a window rather than a number: you see where you are and what
 * is on either side, and the shape changes as you pass it, so you hear every
 * one on the way. */
static void ui_body_shapes(void)
{
    uint8_t sh = fm1_drone_get_param(ui_voice(), P_SHAPE);
    uint8_t cat = fm1_shape_cat(sh);
    int32_t row;
    char line[56];

    for (row = -2; row <= 2; row++) {
        int32_t idx = (int32_t)sh + row;
        uint32_t i = 0;

        line[0] = 0;
        if (idx >= 0 && idx < FM1_NSHAPES) {
            ui_str(line, &i, row == 0 ? "> " : "  ");
            ui_u8((uint8_t)idx, line + i);
            i += 3;
            ui_str(line, &i, "  ");
            ui_str(line, &i, FM1_SHAPE_NAME[idx]);
            line[i] = 0;
        }
        draw_text_box(8, Y_BODY + (row + 2) * 17, 232, &FONT_S, line,
                      row == 0 ? C_ACC : C_GREY, 0);
    }

    /* The category, and the two facts that are invisible otherwise. */
    {
        uint32_t i = 0;
        line[0] = 0;
        ui_str(line, &i, "< ");
        ui_str(line, &i, FM1_SHAPE_CAT[cat].name);
        ui_str(line, &i, " >");
        if (fm1_shape_quantised(sh)) ui_str(line, &i, "   STEPPED");
        if (fm1_shape_is_chord(sh))  ui_str(line, &i, "   CHORD");
        line[i] = 0;
        draw_text_box(8, Y_BODY + 90, 232, &FONT_S, line, C_ON, 0);
    }
}

/* Where this node's signal goes. Routing is a property of a thing, so it sits
 * with that thing -- there is no patchbay page. */
static void ui_body_route(int dest_id)
{
    uint8_t dest = ui_get(dest_id);
    char line[40];
    uint32_t i = 0;

    ui_str(line, &i, "OUT   ");
    ui_str(line, &i, FM1_DESTNAME[dest % DEST_NDEST]);
    line[i] = 0;
    draw_text_box(8, Y_BODY + 8, 232, &FONT_S, line,
                  dest == DEST_OFF ? C_GREY : C_ON, 0);

    if (dest == DEST_OFF)
        draw_text_box(8, Y_BODY + 32, 232, &FONT_S,
                      "UNPLUGGED - NOT HEARD", C_GREY, 0);

    /* A cable pointing back to a lower-numbered channel, or to itself, is a
     * feedback loop and costs exactly one sample. That is 22 us and inaudible
     * as colouration -- but an invisible one-sample delay is precisely what
     * produces a complaint about "weird phasing" six months later. */
    if (ui_nav.menu == M_FX && dest >= DEST_FX1 && dest <= DEST_FX4
        && (uint8_t)(dest - DEST_FX1) <= ui_nav.sel[SUBJ_FX])
        draw_text_box(8, Y_BODY + 56, 232, &FONT_S,
                      "FEEDBACK  +1 SAMPLE", C_ACC, 0);
}

/* --- the mixer ------------------------------------------------------------
 *
 * One page, all four channels. A mixer spread across four pages is not a
 * mixer: the whole value of the layout is seeing the balance, and you cannot
 * balance what you cannot see together.
 */
/* The geometry is tight and the arithmetic is written out because getting it
 * wrong is invisible until it is on screen: the first version put the
 * destination row at y=210, on top of the keyboard strip, where ui_body_clear
 * (which stops at Y_KEYS) could never erase it again.
 *
 *   body        Y_KEYS - Y_BODY            = 118 px
 *   ladder      9 segments x (7 + 2)       =  81
 *   number row                             =  17
 *   destination row                        =  16
 *                                            ---
 *                                            114, with 4 to spare
 */
#define MTR_X0    22
#define MTR_PITCH 56
#define MTR_SEGH  7
#define MTR_GAP   2
#define MTR_H     (FM1_METER_SEGS * (MTR_SEGH + MTR_GAP))
#define MTR_BOT   (Y_BODY + MTR_H)
#define MTR_NUM_Y (MTR_BOT + 2)
#define MTR_DST_Y (MTR_NUM_Y + 17)

static void ui_body_mixer(void)
{
    uint8_t ch;

    for (ch = 0; ch < UI_VOICES; ch++) {
        int32_t x = MTR_X0 + (int32_t)ch * MTR_PITCH;
        uint8_t peak = fm1_drone_meter(ch);
        uint8_t clip = fm1_drone_clipping(ch);
        uint8_t lvl  = fm1_drone_get_param(ch, P_LEVEL);
        uint8_t seg;
        char lab[16];
        uint32_t i = 0;

        /* Nine segments, lit from the bottom. Red is the WHOLE ladder, not
         * the top segment: an overload should be readable from across a room,
         * not found by inspecting one pixel. */
        for (seg = 0; seg < FM1_METER_SEGS; seg++) {
            int32_t y = MTR_BOT - (int32_t)(seg + 1) * (MTR_SEGH + MTR_GAP);
            uint16_t col = C_TRACK;
            if (clip)                              col = C_RED;
            else if (peak >= FM1_METER_STEPS[seg]) col = (seg < FM1_METER_GREEN)
                                                         ? C_GREEN : C_BLUE;
            lcd_fill(x, y, 10, MTR_SEGH, col);
        }

        /* The fader, beside its meter. Vertical, like the hardware it is
         * imitating -- a horizontal bar reads as a spectrum analyser, which
         * is a different instrument telling you a different thing. */
        {
            int32_t cap = MTR_BOT - 4 - ((int32_t)lvl * (MTR_H - 8)) / 127;
            lcd_fill(x + 15, MTR_BOT - MTR_H, 2, MTR_H, C_TRACK);
            lcd_fill(x + 12, cap, 9, 4, ch == ui_voice() ? C_ACC : C_MID);
        }

        /* Channel number and where it goes. Read-only here: the strips are
         * terminal and always reach main, so routing is set where the routed
         * thing lives. */
        lab[0] = (char)('1' + ch);
        lab[1] = 0;
        draw_text_box(x - 8, MTR_NUM_Y, 38, &FONT_S, lab,
                      ch == ui_voice() ? C_ACC : C_GREY, 1);
        i = 0;
        ui_str(lab, &i, FM1_DESTNAME[fm1_drone_get_param(ch, P_DEST) % DEST_NDEST]);
        lab[i] = 0;
        draw_text_box(x - 8, MTR_DST_Y, 38, &FONT_S, lab, C_GREY, 1);
    }

    /* The selected strip is boxed, and the box IS the mapping: it says "these
     * knobs belong to this strip" without a word of explanation.
     *
     * Every edge stays inside Y_BODY .. MTR_DST_Y. The first version put the
     * top edge four pixels above Y_BODY, outside what ui_body_clear erases,
     * so moving SELECT left the old box's top line on screen for good -- the
     * same mistake as the destination row, one pixel row at a time. */
    {
        int32_t x = MTR_X0 + (int32_t)ui_voice() * MTR_PITCH - 10;
        int32_t y = Y_BODY;
        int32_t h = MTR_DST_Y - Y_BODY - 2;
        lcd_fill(x, y, 44, 1, C_RED);
        lcd_fill(x, y + h, 44, 1, C_RED);
        lcd_fill(x, y, 1, h, C_RED);
        lcd_fill(x + 43, y, 1, h, C_RED);
    }
}

/* --- the envelope ---------------------------------------------------------
 *
 * The one parameter group where the picture IS the parameter. Four numbers
 * do not tell you that a long attack running straight into a decay never
 * arrives anywhere -- the two curves meet and the peak is a corner you
 * cannot hear -- but the shape does, immediately.
 *
 * The widths are proportional to the raw 0..127 values rather than to the
 * times they mean. The times are exponential across 1.9 ms to 32.4 s, so a
 * truthful x-axis would render every short segment as nothing at all and the
 * picture would stop being a picture. This axis answers "which segment
 * dominates", which is the question the shape is being read for.
 */
#define ENV_CV_H  64
#define ENV_SUS_W 40        /* sustain holds, so its width means nothing */

static void ui_body_env(void)
{
    uint8_t v = ui_voice();
    uint8_t a = fm1_drone_get_param(v, P_ATTACK);
    uint8_t h = fm1_drone_get_param(v, P_HOLD);
    uint8_t dc = fm1_drone_get_param(v, P_DECAY);
    uint8_t s = fm1_drone_get_param(v, P_SUSTAIN);
    uint8_t r = fm1_drone_get_param(v, P_RELEASE);
    uint8_t stage = fm1_drone_env_stage(v);

    int32_t span = 224 - ENV_SUS_W;
    int32_t tot = (int32_t)a + h + dc + r;
    int32_t wa, wh, wd, wr, x = 8;
    int32_t top = 4, bot = ENV_CV_H - 6;
    int32_t sy = bot - ((int32_t)s * (bot - top)) / 127;
    uint16_t line = C_ACC;

    /* Every segment gets a minimum, so a zero-length one is still visible as
     * a corner. Without it an AD patch with no hold looks like a different
     * envelope rather than the same one with a segment closed. */
    wa = 6 + (tot ? ((int32_t)a  * (span - 24)) / tot : 0);
    wh = 6 + (tot ? ((int32_t)h  * (span - 24)) / tot : 0);
    wd = 6 + (tot ? ((int32_t)dc * (span - 24)) / tot : 0);
    wr = 6 + (tot ? ((int32_t)r  * (span - 24)) / tot : 0);

    cv_begin(240, ENV_CV_H, C_BLACK);
    cv_line(0, bot, 239, bot, C_TRACK);                 /* the floor */

    cv_line(x, bot, x + wa, top, line);          x += wa;    /* attack  */
    cv_line(x, top, x + wh, top, line);          x += wh;    /* hold    */
    cv_line(x, top, x + wd, sy,  line);          x += wd;    /* decay   */
    cv_line(x, sy,  x + ENV_SUS_W, sy, line);    x += ENV_SUS_W;
    cv_line(x, sy,  x + wr, bot, line);                      /* release */
    cv_blit(0, Y_BODY);

    /* Which segment the voice is in now. On a drone an envelope is often
     * minutes long, so this is a real question rather than a decoration. */
    {
        /* Six cells across 240 px is 40 each, which is four characters.
         * Spelling them out gave "USTAI ELEAS" on screen -- the same
         * overlap as the knob labels, for the same reason: nothing clips. */
        static const char *const NAME[FD_ENV_STAGES] = {
            "IDLE", "ATK", "HOLD", "DEC", "SUS", "REL"
        };
        uint8_t k;
        for (k = 0; k < FD_ENV_STAGES; k++)
            draw_text_box((int32_t)k * 40, Y_BODY + ENV_CV_H + 6, 40, &FONT_S,
                          NAME[k], k == stage ? C_ACC : C_TRACK, 1);
    }
}

/* All four voices at once: the page you return to to see the whole
 * instrument. Not editable -- that is what the other menus are for. */
static void ui_body_home(void)
{
    uint8_t v;

    for (v = 0; v < UI_VOICES; v++) {
        char line[56], nb[8];
        uint32_t i = 0;
        uint8_t sh = fm1_drone_get_param(v, P_SHAPE);
        uint8_t dest = fm1_drone_get_param(v, P_DEST);

        ui_note_text(ui_note_of(v), nb);

        line[i++] = (char)('1' + v);
        line[i++] = ' ';
        ui_str(line, &i, nb);
        while (i < 8u) line[i++] = ' ';
        ui_str(line, &i, FM1_SHAPE_NAME[sh % FM1_NSHAPES]);
        while (i < 26u) line[i++] = ' ';
        ui_str(line, &i, FM1_DESTNAME[dest % DEST_NDEST]);
        line[i] = 0;

        draw_text_box(6, Y_BODY + (int32_t)v * 22, 234, &FONT_S, line,
                      v == ui_voice() ? C_ACC
                                      : (dest == DEST_OFF ? C_GREY : C_ON), 0);
    }
}

static void ui_draw_body(void)
{
    const fm1_page_t *p = fm1_nav_page(&ui_nav);
    int alg;

    ui_body_clear();

    if (ui_nav.menu == M_MIX)  { ui_body_mixer(); return; }
    if (ui_nav.menu == M_HOME) { ui_body_home();  return; }
    if (ui_nav.menu == M_ENV)  { ui_body_env();   return; }

    if (p->alg == P_SHAPE && FM1_MENU[ui_nav.menu].scope == SCOPE_VOICE) {
        ui_body_shapes();
        return;
    }

    /* A page whose first knob IS the destination is the routing page, in
     * either menu -- asked of the table rather than hardcoded per menu, so
     * adding a routed thing later needs no change here. */
    {
        int k0 = fm1_page_knob(&ui_nav, 0);
        const fm1_pdef_t *d0 = fm1_page_def(&ui_nav, k0);
        if (d0 && d0->kind == K_DEST) { ui_body_route(k0); return; }
    }

    /* Otherwise say what ALGORITHM is holding: an enum shown only as a number
     * on the value row is an enum nobody can read. */
    alg = fm1_page_alg(&ui_nav);
    if (alg != PG_NONE) {
        const fm1_pdef_t *d = fm1_page_def(&ui_nav, alg);
        char line[40], val[20];
        uint32_t i = 0;
        ui_fmt(alg, val);
        ui_str(line, &i, d ? d->name : "");
        ui_str(line, &i, "   ");
        ui_str(line, &i, val);
        line[i] = 0;
        draw_text_box(8, Y_BODY + 8, 232, &FONT_S, line, C_ON, 0);
    }
}

static void ui_draw(void)
{
    ui_draw_header();
    ui_draw_knobs();
    ui_draw_body();
}

/* --- LEDs -----------------------------------------------------------------
 * A menu button is lit while its menu is open and BLINKS when there are pages
 * behind the one showing. That blink is the only hint a menu has more than
 * one page -- without it, pages beyond the first are invisible. */
static void ui_leds(void)
{
    static const uint8_t BTN[M_NMENU] = {
        UI_B_EDIT, UI_B_ENV, UI_B_FX, UI_B_MIX, UI_B_GLO, UI_B_HOME, UI_B_SAVE
    };
    uint8_t m;
    uint32_t blink = (fm1_ms / 400u) & 1u;

    for (m = 0; m < M_NMENU; m++) {
        uint8_t on = (uint8_t)(m == ui_nav.menu);
        if (on && fm1_menu_has_more(m) && blink) on = 0;
        fm1_led_key(BTN[m], on);
    }
    fm1_led_key(UI_B_SHIFT, ui_shift ? 1u : 0u);
    /* LFO and MOD have no menus yet. Dark rather than lit-and-inert: a lit
     * button that does nothing is worse than an unlit one. */
    fm1_led_key(UI_B_LFO, 0);
    fm1_led_key(UI_B_MOD, 0);
}

/* --- encoders -------------------------------------------------------------
 * Returns non-zero if anything changed, so the caller can redraw and start
 * the save timer. */
static int ui_encoders(void)
{
    int changed = 0;
    int32_t e;
    uint8_t k;

    /* SELECT only ever chooses the Subject. Turning it never edits a value --
     * that is invariant 3, and the reason you can reach for it blind. */
    if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
        fm1_nav_select(&ui_nav, e);
        changed = 1;
    }

    if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
        int alg = fm1_page_alg(&ui_nav);
        if (alg != PG_NONE) {
            /* SHIFT jumps whole categories, which is what makes 48 shapes
             * navigable: three detents instead of thirty to get from a saw
             * stack to a bowed string. */
            if (ui_shift && alg == P_SHAPE) {
                uint8_t sh = fm1_drone_get_param(ui_voice(), P_SHAPE);
                int32_t c = (int32_t)fm1_shape_cat(sh) + (e > 0 ? 1 : -1);
                if (c < 0) c = 0;
                if (c >= FM1_NSHAPE_CATS) c = FM1_NSHAPE_CATS - 1;
                fm1_drone_set_param(ui_voice(), P_SHAPE,
                                    FM1_SHAPE_CAT[c].first);
            } else {
                ui_nudge(alg, e);
            }
            changed = 1;
        }
    }

    for (k = 0; k < FM1_KNOBS; k++) {
        if ((e = fm1_enc_take((uint8_t)(ENC_KNOB1 + k))) != 0) {
            int id = fm1_page_knob(&ui_nav, k);
            if (id != PG_NONE) { ui_nudge(id, e); changed = 1; }
        }
    }

    /* PRESETS is reserved for patch select (UX_DESIGN section 2) and stays
     * unbound until Phase 4 builds slots. Binding it to something it is not
     * would contradict the printed overlay, which is worse than a control
     * that waits. */
    (void)fm1_enc_take(ENC_PRESETS);

    return changed;
}
