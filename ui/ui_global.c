/* SPDX-License-Identifier: MPL-2.0
 * DriftSeq: the GLOBAL page (GLO+HOME). Everything here is one number for the
 * whole instrument: tempo, both halves of swing, and the two switches that are
 * not about the pattern at all (the local GM synth, the radio).
 *
 * Why a page rather than knobs on the panel: these change a few times a
 * session, and the four knobs are the performance surface. John 2026-10-30:
 * "入口用GLO+HOME，FX和LFO都想留作他用。反正GLOBAL不是很频繁". The door is a
 * modifier chord, so it cannot be hit by accident - which is also the reason it
 * must be a chord and not one of the two free buttons.
 */
#define GL_NROW  5
#define GL_Y0    30
#define GL_PITCH 24
#define GL_TEMPO 0
#define GL_SWING 1
#define GL_TRK   2
#define GL_GM    3
#define GL_BT    4

/* G5, the rightmost white key: out of the way of the four knobs, at the right
 * edge where a tempo tap belongs, and idle on a settings page. */
#define GL_TAP_KEY 26

static uint8_t gl_row;
/* 1 = the page was opened by the chord whose HOME has not been released yet. The
 * release of THAT press must not count as "a bare HOME leaves": ui_input walks
 * the buttons in id order, so GLO (5) releases before HOME (6) and clears
 * glo_held first - letting the opening chord close the page in the same frame,
 * which is exactly what happened the first time this page was driven through the
 * web build (the host script let GLO go last and hid it). */
static uint8_t gl_opening;

static const char *const GL_LABEL[GL_NROW] = {"TEMPO", "SWING", "TRK SWING", "GM SYNTH", "BLUETOOTH"};

/* ---- tempo ---------------------------------------------------------------
 * 1 BPM per detent, 10 with GLO held (John 2026-10-30). GLO is already the
 * coarse modifier everywhere on the panel, so this adds no new grammar.
 * ds_tick reads s->bpm on every tick, so a change lands on the next one - no
 * restart, no rewind, and the playhead does not jump (pos16 is in cells).
 */
#define GL_BPM_MIN 20
#define GL_BPM_MAX 300
#define GL_TAPS    4                     /* intervals averaged: one jitters by a
                                          * frame, four settle inside 1 BPM */
#define GL_TAP_GAP 2000u                 /* a longer pause starts a new measurement */

static struct {
    uint32_t t[GL_TAPS + 1];             /* the last GL_TAPS+1 taps = GL_TAPS intervals */
    uint8_t n, w;
} gl_tap;

static void gl_tempo_step(int32_t det)
{
    int bpm = (int)(eng.bpm + 0.5) + (int)det * (glo_held ? 10 : 1);
    if (bpm < GL_BPM_MIN) bpm = GL_BPM_MIN;
    if (bpm > GL_BPM_MAX) bpm = GL_BPM_MAX;
    if ((double)bpm == eng.bpm)
        return;
    ds_set_bpm(&eng, (double)bpm);
    gl_tap.n = 0;                        /* a hand-set tempo ends the tap average:
                                          * the next tap starts a new measurement */
}

/* The tap is the key's DOWN edge. Tempo = 60000 / mean interval: the panel has
 * no better resolution than a millisecond, and 4 intervals put the estimate
 * well inside the 1 BPM the knob moves in. */
static void gl_tap_tap(void)
{
    uint32_t now = plat_ms(), sum = 0;
    int i, k;

    /* The OLDEST tap is at w-n (w counts every tap, n how many are still in the
     * buffer). Getting this wrong - reading w+GL_TAPS-n, which is an empty slot
     * until the buffer fills - makes every tap look like a pause and the tempo
     * never moves. */
    if (gl_tap.n && (uint32_t)(now - gl_tap.t[(uint32_t)(gl_tap.w - gl_tap.n) % (GL_TAPS + 1)]) > GL_TAP_GAP) {
        gl_tap.n = 0;                    /* a pause: not the same tempo any more */
        gl_tap.w = 0;
    }
    gl_tap.t[gl_tap.w % (GL_TAPS + 1)] = now;
    gl_tap.w++;
    if (gl_tap.n < GL_TAPS + 1)
        gl_tap.n++;
    if (gl_tap.n < 2)
        return;                          /* one tap is not a tempo */
    for (k = 0, i = 0; k + 1 < (int)gl_tap.n; k++) {
        uint32_t a = gl_tap.t[(uint32_t)(gl_tap.w + (GL_TAPS + 1) - 1 - k) % (GL_TAPS + 1)];
        uint32_t b = gl_tap.t[(uint32_t)(gl_tap.w + (GL_TAPS + 1) - 2 - k) % (GL_TAPS + 1)];
        if ((uint32_t)(a - b) < 40u)     /* two edges in one frame: not a tap */
            continue;
        sum += a - b;
        i++;
    }
    if (!i)
        return;
    {
        int bpm = (int)(60000.0 * (double)i / (double)sum + 0.5);
        if (bpm < GL_BPM_MIN) bpm = GL_BPM_MIN;
        if (bpm > GL_BPM_MAX) bpm = GL_BPM_MAX;
        ds_set_bpm(&eng, (double)bpm);
    }
}

/* ---- the switches ------------------------------------------------------- */

/* GM on/off is a LISTENER switch: it stops feeding EAS, it does not mute it.
 *
 * Stopping the feed alone would hang: a note sounding at that instant has its
 * note-off queued behind it in eng.out, so the off arrives at a synth that is
 * no longer being fed and the voice renders for ever (EAS has no watchdog).
 * So the switch sends CC123 first, while the feed is still open, and closes
 * afterwards. Re-enabling resumes feeding; sent_prog is NOT cleared, because
 * the receiver's program state did not change either (CC123 is not a reset),
 * and re-sending programs would be the same "who owns this channel" problem
 * that sent_prog exists to solve.
 */
static void gl_gm_set(int on)
{
    if (!on)
        gm_panic();
    glo_gm_on = (uint8_t)(on ? 1 : 0);
    set_gm_set(glo_gm_on);
    app_say("GM SYNTH", on ? "ON" : "OFF");
}

/* Bluetooth on/off, kept with the settings: it decides whether the radio comes
 * up at the next power-on. */
static void gl_bt_set(int on)
{
    plat_ble_enable(on);
    set_ble_set((uint8_t)(on ? 0 : 1));
    app_say("BLUETOOTH", on ? "ON" : "OFF");
}

/* ---- the page ----------------------------------------------------------- */

static void global_encoder(uint8_t enc, int8_t det)
{
    int v;
    ds_track *tr;

    if (enc == EN_K1) {                  /* row: dead stop, no wrap */
        v = (int)gl_row + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > GL_NROW - 1) v = GL_NROW - 1;
        gl_row = (uint8_t)v;
        return;
    }
    if (enc == EN_K3) {                  /* the track being edited */
        sel_track = (uint8_t)clamp((int32_t)sel_track + det, 0, DS_TRACKS - 1);
        gl_row = GL_TRK;
        return;
    }
    if (enc != EN_K2)
        return;                          /* K4 is free on this page */
    switch (gl_row) {
    case GL_TEMPO:
        gl_tempo_step(det);
        break;
    case GL_SWING:
        v = (int)eng.global_swing + det * (glo_held ? 10 : 1);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        eng.global_swing = (uint8_t)v;
        break;
    case GL_TRK:
        tr = &eng.pat.track[sel_track];
        v = (int)tr->swing_amt + det * (glo_held ? 10 : 1);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        tr->swing_amt = (uint8_t)v;
        break;
    case GL_GM:
        if (det)
            gl_gm_set(!glo_gm_on);
        break;
    case GL_BT:
        if (det)
            gl_bt_set(set_ble_off() ? 0 : 1);
        break;
    default:
        break;
    }
}

/* Every key on this page is an audition or a tap, never a grid write: the same
 * rule as TRACK SET (John 2026-10-30). The knobs here change routing, sound and
 * tempo, so the keys must be able to say what it sounds like. */
static uint8_t global_key(uint8_t key, uint8_t down)
{
    int note = 53 + (int)key + key_shift;
    if (key == GL_TAP_KEY) {
        if (down)
            gl_tap_tap();
        return 1;
    }
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (down)
        ds_live_on(&eng, sel_track, (uint8_t)note, midi_vel);
    else
        ds_live_off(&eng, sel_track, (uint8_t)note);
    return 1;
}

/* The door: GLO+HOME, on the press edge (see ui_button). GLO's state is latched
 * by ui_input, which never dispatches GLO itself, so glo_held is the whole
 * chord test.
 *
 * A bare HOME while this page is up leaves, on the release edge - the same
 * gesture that leaves every other page, so the page needs no exit legend. The
 * press of that HOME returns 1 too (it is consumed), which is what keeps HOME
 * from toggling HOME/GRID underneath the page it is closing. */
static uint8_t global_button(uint8_t btn, uint8_t released)
{
    if (btn != B_HOME)
        return 0;
    if (ui_mode == M_GLOBAL) {
        if (released && gl_opening) {
            gl_opening = 0;              /* the chord's own release: not a leave */
            return 1;
        }
        if (released && !glo_held) {
            ui_mode = M_HOME;
            set_commit();                /* the page is done with the record */
        }
        return 1;                        /* the page owns HOME while it is up */
    }
    if (!released && glo_held) {         /* the chord: open */
        ui_mode = M_GLOBAL;
        gl_opening = 1;
        gl_row = 0;
        focus_layer = 0;
        return 1;
    }
    return 0;
}

static void draw_global(void)
{
    int i, y;
    char *v = txt_next();

    cv_begin(240, 224, C_BLACK);
    {
        hdr_t h;
        h.ctx = (char *)"GLOBAL";
        h.right = (char *)0;
        h.right_c = TB_TX;
        h.dots = 1;
        h.page = 4;
        h.badge = (char *)0;
        h.badge_c = 0;
        ui_draw_header(&h);
    }
    for (i = 0; i < GL_NROW; i++) {
        const char *val;
        y = GL_Y0 + i * GL_PITCH;
        v = txt_next();
        switch (i) {
        case GL_TEMPO:
            fmt_int(v, (int32_t)(eng.bpm + 0.5));
            val = v;
            break;
        case GL_SWING:
            fmt_int(v, (int32_t)eng.global_swing);
            strcpy_(txt_end(v), "%");
            val = v;
            break;
        case GL_TRK:
            /* The per-track WEIGHT, with the track it belongs to: the number is
             * meaningless without it, and naming the track here is cheaper than
             * a second line saying which one K3 picks. */
            fmt_int(v, (int32_t)eng.pat.track[sel_track].swing_amt);
            strcpy_(txt_end(v), "% ON T");
            fmt_int(txt_end(v), (int32_t)sel_track + 1);
            val = v;
            break;
        case GL_GM:
            val = glo_gm_on ? "ON" : "OFF";
            break;
        default:
            val = set_ble_off() ? "OFF" : "ON";
            break;
        }
        /* The edited row gets the same band TRACK SET draws, in the same place:
         * the two pages are the same grammar (a list of rows, K1 walks them, K2
         * edits), so a player who has learned one must not have to relearn where
         * "you are here" is drawn on the other. */
        if (i == (int)gl_row)
            cv_rect(4, y - 6, 232, 21, RGB565(26, 26, 34));
        cv_text(12, y, &FONT_S, GL_LABEL[i], (i == (int)gl_row) ? C_AMB : C_GRAY);
        cv_text(100, y, &FONT_B, val, BRIGHTTX);
    }
    /* One fact, said once: SWING is global x track x 50%, so both rows above are
     * halves of one number (that is why the per-track half is not on TRACK SET
     * any more). */
    cv_text(12, 152, &FONT_T, "SWING = GLOBAL X TRK X 50%", RGB565(120, 120, 134));
    cv_text(12, 170, &FONT_T, "TAP TEMPO: RIGHTMOST KEY", RGB565(120, 120, 134));
    cv_blit_band(0, 0, 216);
    ui_draw_footer();
}
