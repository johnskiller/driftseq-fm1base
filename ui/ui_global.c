/* SPDX-License-Identifier: MPL-2.0
 * DriftSeq: the GLOBAL pages (GLO+HOME).
 *
 * Page 1 is one number for the whole instrument (tempo, the global half of
 * swing, and whether the built-in synth listens), plus the machine's own
 * readings. Page 2 is the radio: its switch, and the other role the stack can
 * play - the FM-1 as a BLE central, looking for a MIDI device (a Yamaha MD-BT01
 * on an MPC's DIN port, a BLE keyboard) and connecting to it.
 *
 * The switch went to page 2 on John's reading of the first draft (2026-10-30):
 * "BLE的开关可以移到第二页，这样第一页空间大一点，第二页也不至于这么空旷". A
 * radio switch and the list of what the radio found are one subject, and page 1
 * is the other subject. Space was the visible reason; the real one is that page
 * 1's rows are numbers a player reads at a glance, and a row that is not a
 * number diluted them.
 *
 * Why pages rather than knobs on the panel: these change a few times a session,
 * and the four knobs are the performance surface. John 2026-10-30: "入口用
 * GLO+HOME，FX和LFO都想留作他用。反正GLOBAL不是很频繁". The door is a modifier
 * chord, so it cannot be hit by accident - which is also why it must be a chord
 * and not one of the two free buttons. The same chord flips between the two
 * pages ("可以把BLE扫描链接做在第二个GLOBAL页，GLO+HOME在这两页间切换").
 */
#define GL_NROW  3
#define GL_Y0    30
#define GL_PITCH 34                      /* three rows over the span four used to
                                          * take: same grammar, more air (John:
                                          * "这样第一页空间大一点") */
/* Where the value column starts. It cannot be a round 100: "GM SYNTH" in FONT_M
 * ends at x=124, so a value at 100 was drawn THROUGH the label (John 2026-10-30:
 * "第一页的value需要向右移动一点，现在和左边的label重叠了，主要是第三行GM SYNTH
 * 比较长"). 136 leaves 12px of air after the longest label and still ends a
 * three-digit tempo well inside the band's right edge at 236. */
#define GL_VALX  136
#define GL_TEMPO 0
#define GL_SWING 1
#define GL_GM    2

/* G5, the rightmost white key: out of the way of the four knobs, at the right
 * edge where a tempo tap belongs, and idle on a settings page. */
#define GL_TAP_KEY 26

/* Page 2: one row per device (btdev keeps up to 8), the list under a title line
 * that says what the scan is doing. Row 0 is the first device, so K2 walking the
 * rows IS btdev_choose - no second selector to keep in step. */
#define GL2_DEVS 8
#define GL2_BT   0                       /* row 0: the radio itself, not a device */

/* gl_page and gl_row are declared in ui_glue.c: ui_screens.c's footer painter has
 * to read them (page 2 offers K2 only on the switch row) and that file is
 * compiled first. */
static uint8_t gl2_was_n;                /* devices found at the last frame: a change redraws */
/* 1 = the page was opened by the chord whose HOME has not been released yet. The
 * release of THAT press must not count as "a bare HOME leaves": ui_input walks
 * the buttons in id order, so GLO (5) releases before HOME (6) and clears
 * glo_held first - letting the opening chord close the page in the same frame,
 * which is exactly what happened the first time this page was driven through the
 * web build (the host script let GLO go last and hid it). */
static uint8_t gl_opening;

/* GLOBAL holds the GLOBAL half of each number and nothing else (John
 * 2026-10-30: "GLOBAL页可以选trk改trk的swing比较奇怪。还是GLOBAL只改全局swing，
 * track set改当前track的"). TRK SWING lived here for an hour and asked K3 to go
 * pick a track on a page that is otherwise about the whole instrument; it is
 * back on TRACK SET, where the edited track is the page's subject. */
static const char *const GL_LABEL[GL_NROW] = {"TEMPO", "SWING", "GM SYNTH"};

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

/* ---- the switches -------------------------------------------------------
 *
 * A two-state row is a SLIDER with two detents, not a toggle: clockwise asks for
 * ON, counter-clockwise asks for OFF, and the knob dead-stops at both ends
 * because there is no third state to reach. The panel already works this way -
 * K3's MUTE/SOLO/ON is a position that dead-stops (ui.c k3_state_slider) - so a
 * detent that flipped whatever the direction was made these two rows the only
 * places where turning the knob left did not move the value left. John
 * 2026-10-30: "第一页的GM，第二页的BLE on/off选择都没有做到盲拧效果".
 *
 * So `det > 0` asks for ON, `det < 0` asks for OFF, and asking for the state it
 * is already in does nothing.
 */
static int gl_want(int on, int8_t det)
{
    if (det > 0) return 1;
    if (det < 0) return 0;
    return on;
}


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
 * up at the next power-on. Off also ends a link to a device: the list under it
 * has nothing to look for with the radio quiet.
 *
 * It lives on page 2 (John 2026-10-30), as row 0 of the same list grammar the
 * devices use - so the page's one knob still means "walk the list", and the
 * switch is the first thing the hand meets on the page whose subject it is. */
static void gl_bt_set(int on)
{
    plat_ble_enable(on);
    set_ble_set((uint8_t)(on ? 0 : 1));
    app_say("BLUETOOTH", on ? "ON" : "OFF");
}

/* ---- page 2: the radio --------------------------------------------------
 * The same grammar as page 1, which is the whole point of putting the switch
 * here: K1 walks the rows, K2 edits the value on the row you are on. Row 0 is
 * the switch (K2 toggles the radio), rows 1.. are the devices the scan found,
 * and SAVE is the verb on a device - connect, or let go (btdev's own verb).
 *
 * The switch used to be row 3 of page 1 and the list walked with K2, so the two
 * pages had two different hands for the same gesture. One list, one grammar:
 * K1 walks, K2 edits, SAVE commits. Row 0 is a row, so the knob that walks the
 * list cannot be the knob that edits it.
 */

/* Rows on page 2 = the switch, then either the LINK (while a device is being
 * connected to or is connected) or the devices the scan has found.
 *
 * The link gets its own row because it is NOT in the scan list: btdev stops
 * scanning once a device is connected, so btdev_count() drops to 0 at exactly the
 * moment the player most wants a row to point at. Without this the cursor fell
 * back to the switch and the page could no longer let the device go - the test
 * caught it (SAVE connected fine, then there was nothing left to disconnect). */
static uint8_t gl2_link(void)
{
    return (uint8_t)(!set_ble_off() && btdev_busy());
}

static uint8_t gl2_rows(void)
{
    int n = set_ble_off() ? 0 : (int)btdev_count();
    if (gl2_link())
        return 2;                        /* the switch + the link */
    return (uint8_t)(1 + (n > GL2_DEVS ? GL2_DEVS : n));
}

/* The device on the selected row, as btdev's index: row 0 is the switch and has
 * no device behind it. While the link row is up it stands for device 0: that is
 * the one btdev_save would act on, and it is the one it will let go. */
static int gl2_dev(void)
{
    return gl2_link() ? 0 : (int)gl_row - 1;
}

/* The device row's verb, on K2 like every other way of changing a value on a
 * settings page (docs/UI_GRAMMAR.md: 设定页面 K1 row, K2 value). Right connects
 * the picked device, left lets a connected one go.
 *
 * It used to be SAVE. John 2026-10-31: "ble连接不应该用save按钮，而应该用k2来连接/断开".
 * The reason it is K2 and not merely a moved key: connecting is what that row's
 * value IS. The row reads CONNECTED / LET GO / PAIRING, and K2 is the knob that
 * changes a row's value on every other page.
 *
 * A direction that does not apply does nothing and says nothing: turning right
 * on an established link is not a second connect, and turning left on an idle
 * one is not a second goodbye. Re-asking the radio for a state it already has is
 * what the two-state rows elsewhere (GM, the Bluetooth switch) avoid too. */
static void gl2_verb(int8_t det)
{
    int ev;
    if (gl_row == GL2_BT || set_ble_off()) {
        /* the switch row is the radio's, not a device's; K2 on it is gl_bt_set's */
        return;
    }
    if (det > 0) {
        if (gl2_link())
            return;                      /* already linked: not a second connect */
        btdev_choose((uint8_t)gl2_dev());
        ev = btdev_save();
        if (ev == BTDEV_CONNECTING)
            app_say("BLE DEVICE", btdev_name());
        else
            app_say("BLE DEVICE", btdev_word(ev));
    } else {
        if (!gl2_link())
            return;                      /* nothing to let go */
        ev = btdev_save();
        app_say("BLE DEVICE", ev == BTDEV_LET_GO ? "DISCONNECTED" : btdev_word(ev));
    }
    ui.redraw = 1;
}

/* What the page says above the list. btdev's own words are about the LINK; these
 * are about the LIST, which is the page's subject. */
static const char *gl2_state(char *b)
{
    int d = plat_ble_device();
    if (set_ble_off())
        return "RADIO OFF";
    if (plat_ble_state() != BLE_READY)
        return "RADIO STARTING";
    switch (d) {
    case BLE_DEV_SCANNING:   return "SCANNING";
    case BLE_DEV_CONNECTING: strcpy_(b, "CONNECTING "); strcpy_(txt_end(b), btdev_name()); return b;
    case BLE_DEV_SEARCHING:  return "FINDING MIDI";
    case BLE_DEV_READY:      strcpy_(b, "CONNECTED "); strcpy_(txt_end(b), btdev_name()); return b;
    default:                 return "NOT CONNECTED";
    }
}

/* ---- the page ----------------------------------------------------------- */

static void global_encoder(uint8_t enc, int8_t det)
{
    int v, last;

    if (enc == EN_K1) {                  /* row: dead stop, no wrap. The SAME
                                          * gesture on both pages - one hand, one
                                          * grammar, which is why the switch
                                          * could move to page 2 at all. */
        last = gl_page ? (int)gl2_rows() - 1 : GL_NROW - 1;
        v = (int)gl_row + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > last) v = last;
        if (v == (int)gl_row)
            return;
        gl_row = (uint8_t)v;
        if (gl_page && gl_row != GL2_BT)
            btdev_choose((uint8_t)gl2_dev());
        ui.redraw = 1;
        return;
    }
    if (enc != EN_K2)
        return;                          /* K4 is free on both pages */
    if (gl_page) {
        /* Row 0 is the switch, and K2 turns it. A device row has no value to
         * turn: its verb is SAVE, because connecting is not a number. */
        /* Row 0's value is a two-detent slider (see gl_want): right asks for ON,
         * left asks for OFF. set_ble_off() is 1 when the radio is stored OFF, so
         * the "on" it is in is !set_ble_off(). */
        if (gl_row == GL2_BT) {
            int on = gl_want(!set_ble_off(), det);
            if (on != (set_ble_off() ? 0 : 1))
                gl_bt_set(on);
        } else {
            gl2_verb(det);               /* a device row: its value is the link
                                          * itself (right connect, left let go) */
        }
        return;
    }
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
    case GL_GM:
        /* Same slider: right = ON, left = OFF, and no change when it is already
         * there (re-sending CC123 on every detent would panic a synth that is
         * already silent). */
        {
            int on = gl_want(glo_gm_on, det);
            if (on != glo_gm_on)
                gl_gm_set(on);
        }
        break;
    default:
        break;
    }
}

/* Every key on this page is an audition or a tap, never a grid write: the same
 * rule as TRACK SET (John 2026-10-30). The knobs here change routing, sound and
 * tempo, so the keys must be able to say what it sounds like. On page 2 the keys
 * are a keybed too - a device that just connected needs to be heard. */
static uint8_t global_key(uint8_t key, uint8_t down)
{
    int note = 53 + (int)key + key_shift;
    if (!gl_page && key == GL_TAP_KEY) {
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
 * While the page is up, the SAME chord flips between its two pages, and a bare
 * HOME leaves, on the release edge - the same gesture that leaves every other
 * page, so the page needs no exit legend. The press of that HOME returns 1 too
 * (it is consumed), which is what keeps HOME from toggling HOME/GRID underneath
 * the page it is closing. */
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
            return 1;
        }
        if (!released && glo_held) {     /* the chord again: the other page */
            /* gl_opening guards the FLIP too, not just the opening: ui_input
             * releases the buttons in id order, so a player who lets go of both
             * at once releases GLO (5) before HOME (6), glo_held is already 0 by
             * the time HOME's release arrives, and the release of the very press
             * that flipped the page would read as a bare HOME and close it. The
             * host script released HOME first and hid this; the web build, which
             * sets one level per frame and drops both together, closed the page
             * on the second chord every time. */
            gl_opening = 1;
            ui_page_key(M_GLOBAL, ui_mode);   /* rule 4: the same turn every feature gets */
            /* The scan runs while page 2 is up (btdev_page owns that): nothing
             * to start here, but the list's cursor must not point past a list
             * that restarts empty. */
            gl_row = 0;
            ui.redraw = 1;
        }
        return 1;                        /* the page owns HOME while it is up */
    }
    if (!released && glo_held) {         /* the chord: open */
        /* Opening does NOT reset the page (docs/UI_GRAMMAR.md rule 4: a feature
         * remembers its page). It used to force page 1, on the theory that
         * "opening never lands on page 2" - but page 2 is a scan list that starts
         * itself when it is looked at, so a player who was working there and
         * stepped out came back to the settings and had to turn again. The rule
         * is one rule: you arrive where you left. */
        ui_mode = M_GLOBAL;
        gl_opening = 1;
        gl_row = 0;
        focus_layer = 0;
        return 1;
    }
    return 0;
}

/* ---- page 1's readings --------------------------------------------------
 * The bottom of page 1 used to be three notes about swing and tap. The notes
 * are one line each on the page that owns the fact (that is the rule the whole
 * screen follows), and the rest of the space is better spent on the machine:
 * what the render costs, whether it has ever missed its deadline, how much of
 * the pattern store is in use, and what is running.
 *
 * FW VERSION: there is no version number in a dev build. APP_VERSION is "DEV"
 * unless --release is passed, and the package identity (FELUCCA_ID, e.g.
 * FM-1_9200) is the id the update tools parse, not a version. So this line
 * shows what a dev build actually has: the identity, and the wasm/device split
 * is invisible to the page. A release build prints its X.Y.Z here because
 * APP_VERSION is what unity.c already uses for the splash.
 *
 * CPU: plat_cpu_pct is the render's load, smoothed over ~16 blocks on the
 * device and measured per block on the host. XRUNS is the count of blocks the
 * render missed since boot - 0 on a healthy unit, and the number to quote in a
 * bug report. Both are live, so the page must redraw while it is up.
 */
uint32_t gl_pattern_bytes(void);

static void gl_reading(char *v, int row)
{
    switch (row) {
    case 0:                              /* CPU, as the platform measures it */
        {
            /* plat_cpu_pct() samples the audio idle time on its own 500 ms
             * clock; it is a cheap read, but a value that walks one percent
             * about is noise on a page the player reads while playing, so the
             * display moves in 5% steps and only when it really moved. */
            static int32_t shown = -1;
            int32_t now = (int32_t)plat_cpu_pct();
            if (shown < 0 || now >= shown + 5 || now <= shown - 5)
                shown = now;
            fmt_int(v, shown);
        }
        strcpy_(txt_end(v), "%");
        break;
    case 1:                              /* XRUNS */
        fmt_int(v, (int32_t)plat_xruns());
        break;
    default:                             /* PATTERN, in bytes */
        {
            uint32_t b = gl_pattern_bytes();
            if (b == 0xFFFFFFFFu) {          /* does not fit one store object */
                strcpy_(v, ">");
                fmt_int(txt_end(v), (int32_t)PLAT_STORE_MAX);
            } else {
                fmt_int(v, (int32_t)b);
                strcpy_(txt_end(v), "/");
                fmt_int(txt_end(v), (int32_t)PLAT_STORE_MAX);
            }
            /* the sector it would be written to: the store's own unit, because
             * flash erases 4 KiB at a time and a pattern of 3,900 B still costs
             * a whole one */
            strcpy_(txt_end(v), " ");
            fmt_int(txt_end(v), (int32_t)((b + PLAT_STORE_MAX - 1) / PLAT_STORE_MAX));
            strcpy_(txt_end(v), "S");
        }
        break;
    }
}

/* Packed size of the pattern as it stands, or 0xFFFFFFFF if it does not fit one
 * store object. Measured at most once a second: the packer walks all 16 x 64
 * steps, and page 1 redraws at 60 Hz. The buffer is 3,840 B in the pool (the
 * store object's own size), written and re-read every second - not a copy of the
 * pattern, just room for the packer to prove the size. */
#define GL_PAT_EVERY_MS 1000u
uint32_t gl_pattern_bytes(void)
{
    static uint32_t at, bytes;
    static uint8_t PLAT_POOL buf[DS_PACK_TRACK_MAX];   /* one track's worst case */
    uint32_t now = plat_ms();
    if (!at || (uint32_t)(now - at) >= GL_PAT_EVERY_MS) {
        uint32_t n = 0;
        int t;
        at = now ? now : 1u;
        /* A pattern IS the concatenation of its 16 track records (pack.c:
         * ds_pack_pattern is a loop of ds_pack_track), so summing them needs one
         * track's worth of room instead of the whole store object's - 1,036 B
         * rather than 3,840 B of pool for a number that is only displayed. */
        for (t = 0; t < DS_TRACKS; t++) {
            size_t w = ds_pack_track(buf, sizeof buf, &eng.pat.track[t]);
            if (!w) {                    /* a track that cannot pack: report it as
                                          * too big rather than a wrong number */
                n = 0xFFFFFFFFu;
                break;
            }
            n += (uint32_t)w;
        }
        bytes = n;
    }
    return bytes;
}

static void draw_global1(void)
{
    int i, y;
    char *v;

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
        default:
            val = glo_gm_on ? "ON" : "OFF";
            break;
        }
        /* The edited row gets the same band TRACK SET draws, in the same place:
         * the two pages are the same grammar (a list of rows, K1 walks them, K2
         * edits), so a player who has learned one must not have to relearn where
         * "you are here" is drawn on the other. */
        if (i == (int)gl_row) {
            /* The same two pieces TRACK SET draws: the band, and the 2px amber
             * rule on its left edge. The rule is what makes "you are here" a
             * shape rather than a shade - John 2026-10-30: "Track set页面每行前
             * 有个竖线，配合选中行颜色，很美观，GLOBAL页面也要保持同样的风格". */
            cv_rect(4, y - 6, 232, 25, RGB565(26, 26, 34));
            cv_rect(4, y - 6, 2, 25, C_AMB);
        }
        cv_text(12, y, &FONT_M, GL_LABEL[i], (i == (int)gl_row) ? C_AMB : C_GRAY);
        cv_text(GL_VALX, y, &FONT_M, val, BRIGHTTX);
    }

    /* Three rows of three numbers left room that the four-row draft did not, and
     * the readings are what earns it: they are the machine's own numbers, drawn
     * dim and without a band so no hand tries to turn them. */
    {
        static const char *const GLY[3] = {"CPU", "XRUNS", "PATTERN"};
        char *rb = txt_next();               /* ONE buffer for all three: the pool
                                              * is 8 slots and the three settings
                                              * above already hold three of them */
        for (i = 0; i < 3; i++) {
            y = 146 + i * 17;
            cv_text(12, y, &FONT_T, GLY[i], RGB565(96, 96, 110));
            v = rb;
            *v = 0;
            gl_reading(v, i);
            cv_text(80, y, &FONT_S, v,
                    (i == 1 && plat_xruns()) ? C_AMB : RGB565(150, 150, 164));
        }
    }
    cv_text(12, 200, &FONT_T, "TAP: RIGHTMOST KEY", RGB565(96, 96, 110));
    cv_blit_band(0, 0, 216);
    ui_draw_footer();
}

static void draw_global2(void)
{
    int i, n = (int)gl2_rows(), y;
    char *b = txt_next();

    /* The list owns the body from the top: row 0 is the switch, rows 1.. are the
     * devices. The link's state used to sit ABOVE row 0, where it read as a list
     * row of its own ("SCANNING" looked like a device named SCANNING) - it now
     * shares the bottom line with the SAVE hint, which is the other thing that is
     * about the list rather than in it. */
    for (i = 0; i < n; i++) {
        char name[21] = {0};             /* ONE buffer outside the branches: it is
                                          * read after the branch that fills it,
                                          * and a buffer declared inside the else
                                          * is dead - it segfaulted the first time
                                          * this page was drawn */
        int rssi = 0;

        y = GL_Y0 + 4 + i * 18;
        if (i == (int)gl_row) {              /* band + the same amber rule */
            cv_rect(4, y - 5, 232, 17, RGB565(26, 26, 34));
            cv_rect(4, y - 5, 2, 17, C_AMB);
        }

        if (i == GL2_BT) {
            /* row 0: the radio, drawn in the shape of a settings row - label
             * left, value right, K2 changes it. */
            cv_text(12, y, &FONT_S, "BLUETOOTH", (i == (int)gl_row) ? C_AMB : TB_TX);
            cv_text(150, y, &FONT_B, set_ble_off() ? "OFF" : "ON", BRIGHTTX);
            continue;
        }
        if (gl2_link()) {
            /* row 1 while a link is up: the device, and its state where a found
             * device shows its signal. It is not in the scan list (the scan has
             * stopped), so it must be drawn from btdev_name, not btdev_found. */
            cv_text(12, y, &FONT_S, btdev_name(), (i == (int)gl_row) ? C_AMB : TB_TX);
            cv_text(150, y, &FONT_T,
                    (plat_ble_device() == BLE_DEV_READY) ? "LINKED" : "LINKING",
                    (plat_ble_device() == BLE_DEV_READY) ? C_HI : RGB565(120, 120, 134));
            continue;
        }
        btdev_found(i - 1, name, &rssi);
        cv_text(12, y, &FONT_S, name, (i == (int)gl_row) ? C_AMB : TB_TX);
        /* the signal, as bars rather than -50: a number is not a comparison */
        {
            int bars = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1, k;
            for (k = 0; k < 4; k++)
                cv_rect(224 - (4 - k) * 5, y + 8 - (k + 1) * 2, 3, (k + 1) * 2,
                        (k < bars) ? ((i == (int)gl_row) ? C_AMB : TB_TX) : RGB565(58, 58, 70));
        }
    }
    if (n == 1) {
        /* No device and no link. An empty list under a switch reads as "the scan
         * is broken", and with the radio off it would read the same way, so the
         * page says which of the two it is. */
        cv_text(12, GL_Y0 + 4 + 18, &FONT_S,
                set_ble_off() ? "NO DEVICES" : "LOOKING...",
                RGB565(120, 120, 134));
    }
    /* One line, two facts about the list: what the radio is doing (left) and what
     * SAVE does on the selected row (right). SAVE is not the switch's verb, so on
     * row 0 the right half stays empty rather than naming an action it does not
     * do. */
    cv_text(12, 200, &FONT_T, gl2_state(b),
            (plat_ble_device() == BLE_DEV_READY) ? C_HI : RGB565(96, 96, 110));
    if (gl_row != GL2_BT)
        cv_text(148, 200, &FONT_T, btdev_busy() ? "SAVE: LET GO" : "SAVE: CONNECT",
                RGB565(96, 96, 110));
    cv_blit_band(0, 0, 216);
    ui_draw_footer();                    /* reads gl_page, so it paints DEVICE/ON-OFF
                                          * itself: one blit of the band, not two */
}

static void draw_global(void)
{
    hdr_t h = {0};

    cv_begin(240, 224, C_BLACK);
    h.no_pill = 1;                       /* not a track's page: no number, no pill */
    h.ctx = gl_page ? "GLOBAL 2/2" : "GLOBAL 1/2";
    h.right = (char *)0;
    h.right_c = TB_TX;
    h.dots = 1;
    h.page = 4;
    h.badge = (char *)0;
    h.badge_c = 0;
    ui_draw_header(&h);
    if (gl_page)
        draw_global2();
    else
        draw_global1();
}

/* Called from app_frame every frame: page 2 is the page that scans, so btdev is
 * told whether this page is up. Also keeps the list's cursor honest when the
 * scan finds or loses devices behind the player's back. */
static void global_frame(void)
{
    uint8_t r;

    /* app_frame owns btdev_page/ui.looking (one poll per frame, in that order:
     * this runs before it). Page 2 IS the device page, so it is the page that
     * asks for the scan - and while any other page or page 1 is up the scan
     * stops, which is what keeps the radio quiet when nothing is looking. */
    ui.looking = (uint8_t)(ui_mode == M_GLOBAL && gl_page && !set_ble_off());
    if (!ui.looking) {
        gl2_was_n = 0;
        return;
    }
    /* The list grows and shrinks behind the player's back. The cursor counts
     * rows and row 0 is the switch, so the bound is gl2_rows()-1, not the device
     * count - an off-by-one here would park the cursor on a row that is not
     * there and SAVE would connect to the wrong device. */
    r = gl2_rows();
    if (r != gl2_was_n) {
        /* Clamp to the last row, but never onto row 0 by accident: row 0 is the
         * radio switch, and K2 on it turns the radio OFF. A player who has just
         * let a link go has his hand on K2 and his eye on a device row; pulling
         * the cursor onto the switch means his next detent kills Bluetooth
         * instead of picking another device (found by the test that turns K2
         * left twice: the second turn, meant as a no-op, switched the radio off).
         * So the floor is 1 whenever there is a device row to sit on. */
        int floor = (r > 1) ? 1 : 0;
        if ((int)gl_row > (int)r - 1)
            gl_row = (uint8_t)((int)r - 1);
        if ((int)gl_row < floor)
            gl_row = (uint8_t)floor;
        if (gl_row != GL2_BT)
            btdev_choose((uint8_t)gl2_dev());
        gl2_was_n = r;
        ui.redraw = 1;
    }
    /* The readings on page 1 are live numbers; page 2's list changes on its own
     * while it scans. Both pages need the screen to keep up. */
    ui.redraw = 1;
}

