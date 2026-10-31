/* ui_trkset.c — TRACK SET: the per-track identity (part of the UI's unity
 * section, included after ui_scale.c and before ui_edit.c so that file's
 * routing can call into it).
 *
 * Five rows, each a field of ds_track that had no page of its own, plus the
 * one global that SWING multiplies by:
 *
 *   CH      channel 1..16 — also decides drum vs melodic (10 = drums, the GM
 *           convention is_drum_trk() already enforces)
 *   PROG    the GM program, shown as number + name: "039 SYNTH BASS 1". A bare
 *           0..127 is a number nobody knows; the name is the thing being chosen
 *   ROUTE   USB / GM / USB+GM — where this track's notes go
 *   VOICE   POLY / MONO — MONO plays the top pitch of each step
 *   SWING   0..100% — how much of the global swing this track takes
 *
 * K4 is the GLOBAL swing: a whole-song number, so it gets its own knob instead
 * of a sixth row that would be edited two ways. K3 stays MUTE/SOLO, as on every
 * other page: the hand already knows where that is.
 *
 * The lower half is a CHANNEL MAP. Choosing a channel is choosing to double
 * some other track's channel (GM gives them one program and one voice pool), so
 * the page answers "who else is there" — 16 cells, lit where a track lives,
 * and the sharing line names the other tracks by number.
 *
 * TWO PAGES (2026-10-31, John): "页面可能容纳不下，需要第二页". Page 1 is the five
 * identity rows above; page 2 is the two things that act on the track rather
 * than describe it -- DIVISION and CLEAR TRACK. Both were asked for on this page
 * ("1 change div，现在的div修改位置不是很明显，增加一个更明显的修改方式。2 clear
 * track，这个危险操作需要弹出确认"), and neither fits page 1: five 22 px rows plus
 * the channel map already fill y30..194.
 *
 * DIVISION is not new, it is LOUDER. It lived on GLO+K3 on the grid screens,
 * which is a chord nobody finds (John's words) and is also where the HOME and
 * GRID footer already print "DIV" - a hint pointing at a modifier. As a row it
 * is the same size as every other value on this page.
 */
static uint8_t ts_row;          /* 0..TS_NROW-1, dead-stop */

#define TS_NROW 5
/* 16px rows need a 22px pitch. Five of them (y30..140) leave the channel map at
 * 144, its cells at 158..178 and the bottom line at 186.
 *
 * SWING is this TRACK's weight (2026-10-30). It left the page for a day and came
 * back by John's call: "GLOBAL页可以选trk改trk的swing比较奇怪。还是GLOBAL只改
 * 全局swing，track set改当前track的". A page whose every other row is a property
 * of one track had no business asking K3 to go find a different track. The
 * global half lives on GLOBAL; this is the per-track half, and the row explains
 * itself when the global is zero (the product is zero and the row would look
 * broken otherwise). */
#define TS_Y0    30
#define TS_PITCH 22
#define TS_CH   0
#define TS_PROG 1
#define TS_ROUTE 2
#define TS_VOICE 3
#define TS_SWING 4

static const char *const TS_LABEL[TS_NROW] = {"CHANNEL", "PROGRAM", "ROUTE", "VOICE", "SWING"};

/* ---- page 2: the actions ---- */
#define TS2_NROW  2
#define TS2_DIV   0
#define TS2_CLEAR 1
static const char *const TS2_LABEL[TS2_NROW] = {"DIVISION", "CLEAR TRACK"};
/* ts_row is ONE cursor for both pages: arriving resets it to 0 and K1 dead-stops
 * at the bottom of whichever page is up. Two cursors would mean one of them
 * points somewhere the player cannot see. */

/* GM_PROG (the 128 names) is in ui_tables.c: the PRESET popup needs the same
 * table at a larger size, and two copies would drift. */

/* ROUTE is the WIRE only (USB / BLE / BOTH). The local GM synth hears every
 * track; turning it off is a global mix question, not a per-track destination
 * (John 2026-10-30). */
static const char *const TS_ROUTE_NAME[3] = {"USB", "BLE", "USB+BLE"};

/* On the drum channel a program change picks a KIT, not an instrument, so the
 * PROGRAM row must not say "ACOUSTIC GRAND" on a track whose notes are 36..51.
 * GM_KIT (the nine GM2 kits) lives in ui_tables.c with GM_PROG: PRESETS walks
 * the same nine, and neither file should depend on the other's include order.
 * A kit we have no name for shows just the number. */

/* No resend call here any more. The engine compares each channel's program
 * against what it last told the receiver (sent_prog[]) on every tick while the
 * transport runs, so editing CH or PROG re-states the program by itself - and a
 * track that is NOT its channel's owner changes nothing, which is the whole
 * point of the owner rule. Stopped, nothing is sent: there is no run to send
 * into, and the PC that PLAY emits carries the new number anyway. */

/* the footer (ui_screens.c, included BEFORE this file) names what K2 does, and
 * on page 2 that depends on the row: DIVISION edits a value, CLEAR TRACK opens a
 * decision. A getter rather than moving ts_row up: the cursor is this page's. */
static uint8_t ts_row_on_clear(void) { return ui_page[M_TRKSET] && ts_row == 1; }

static void trkset_encoder(uint8_t enc, int8_t det)
{
    ds_track *tr = TRK();

    if (enc == EN_SELECT) {                      /* the page is per-track: SELECT walks tracks,
                                                  * dead stop at both ends like every other
                                                  * track selector on the panel */
        int v = (int)sel_track + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > DS_TRACKS - 1) v = DS_TRACKS - 1;
        ui_select_track((uint8_t)v);             /* and its voice goes to the wire:
                                                  * the keys audition it now, not
                                                  * on the next PLAY */
        ts_row = 0;                              /* a new track starts at the top */
        return;
    }
    if (enc == EN_K1) {                          /* row: dead stop, no wrap */
        int n = ui_page[M_TRKSET] ? TS2_NROW : TS_NROW;
        int v = (int)ts_row + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > n - 1) v = n - 1;
        ts_row = (uint8_t)v;
        return;
    }
    if (enc == EN_K3) {                          /* MUTE/SOLO, as everywhere */
        k3_state_slider(tr, sel_track, det);
        return;
    }
    if (enc != EN_K2)
        return;
    if (ui_page[M_TRKSET]) {                     /* ---- page 2 ---- */
        if (ts_row == TS2_DIV) {
            /* The same five values, the same dead stops as the GLO+K3 chord;
             * ds_track's division is per track, so this is the track on the
             * header pill. */
            int v = (int)tr->division + (det > 0 ? 1 : -1);
            if (v < DS_DIV_8) v = DS_DIV_8;
            if (v > DS_DIV_32) v = DS_DIV_32;
            tr->division = (uint8_t)v;
        } else {
            /* CLEAR TRACK is the row; the VALUE is the decision, and a value that
             * wipes a track is a decision, not a detent (rule 2). K2 right opens
             * the popup, K2 left does nothing - there is nothing to undo it with,
             * and a left-twist that armed a wipe would be the worst possible
             * surprise on a knob you were just turning to see what it does. */
            if (det > 0)
                popup_open(PM_CLRTRK);
        }
        return;
    }
    switch (ts_row) {
    case TS_CH: {
        int v = (int)tr->channel + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > 15) v = 15;
        if (v != (int)tr->channel) {
            /* the sounding notes keep their own channel (gate/live capture it),
             * so this only affects what is played next — no note hangs */
            tr->channel = (uint8_t)v;
            ds_route_sync(&eng);
            ds_send_prog(&eng, sel_track);       /* the channel moved: its program
                                                  * belongs there too, or the keys
                                                  * audition the previous channel's
                                                  * voice */
        }
        break;
    }
    case TS_PROG: {
        int v = (int)tr->prog + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        if (v != (int)tr->prog) {
            tr->prog = (uint8_t)v;
            ds_send_prog(&eng, sel_track);       /* the keys audition the voice you
                                                  * just chose: a stopped transport
                                                  * sends no PC of its own */
        }
        break;
    }
    case TS_ROUTE: {
        int v = (int)tr->route + (det > 0 ? 1 : -1);
        if (v < DS_ROUTE_USB) v = DS_ROUTE_USB;
        if (v > DS_ROUTE_BOTH) v = DS_ROUTE_BOTH;
        tr->route = (uint8_t)v;
        ds_route_sync(&eng);                     /* the drain reads the masks */
        break;
    }
    case TS_SWING: {
        int v = (int)tr->swing_amt + det * 5;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        tr->swing_amt = (uint8_t)v;
        break;
    }
    case TS_VOICE: {
        int v = (int)tr->voice_mode + (det > 0 ? 1 : -1);
        if (v < DS_VOICE_POLY) v = DS_VOICE_POLY;
        if (v > DS_VOICE_MONO) v = DS_VOICE_MONO;
        if (v != (int)tr->voice_mode) {
            tr->voice_mode = (uint8_t)v;
            eng.mono_note[sel_track] = 0xFF;     /* the other mode holds nothing */
        }
        break;
    }
    default: break;
    }
}

/* the page's SEL/ENV/leave handling. ENV is the door (John's pick: the printed
 * label is a lie either way, and ENV is the one key no page has ever used).
 * Note SEL is not handled here: scale_button runs first and owns it, so SEL
 * from this page goes to SCALE, which is where the panel says it goes. */
static uint8_t trkset_button(uint8_t btn)
{
    if (btn == B_ENV) {
        /* ENV is TRACK SET's door and a page key like the others
         * (docs/UI_GRAMMAR.md rule 4): it used to toggle back to the hero. The row
         * cursor still starts at the top when you arrive, because arriving is
         * what "which row am I on" is about; a second press does not re-home it,
         * since that press is a page turn, not an arrival. */
        uint8_t was = ui_mode;
        ui_page_key(M_TRKSET, ui_mode);
        if (was != M_TRKSET) {
            ts_row = 0;
            focus_layer = 0;
        }
        return 1;
    }
    if (ui_mode != M_TRKSET)
        return 0;
    if (btn == B_HOME)
        return 0;                                /* HOME is HOME's, not this page's:
                                                  * see the ui_button case */
    return 0;
}

/* "039 SYNTH BASS 1" into b — the buffer is the caller's, so this does not
 * juggle the text ring (that ring is 8 deep and the page already uses several). */
/* Wipe the NOTES of one track, and nothing else (John 2026-10-31: "只清音符…清
 * 一个轨不该把轨静默搬到 ch1 / prog0").
 *
 * ds_clear_track() is the wrong tool: it memsets the whole ds_track, so channel,
 * program, division, route, voice mode and swing all snap back to their defaults
 * and the track silently moves to ch1 with the piano. That is what a factory
 * reset does, not what a player means by "clear this". So the note fields are
 * cleared BY NAME - the same clearing ds_clear_track does per step (voice notes
 * to 0xFF = empty, prob back to 100, cond none, on/tie off) - and the identity
 * bytes are never written. A field added to ds_step later defaults to 0 in a
 * memset and would be missed here, so the step is zeroed first and then given
 * its non-zero defaults, which keeps this correct by construction rather than by
 * remembering.
 *
 * euclid's eu_on/eu_k/eu_n/eu_rot are the SKELETON, not the notes: ds_euclid_fill
 * repaints steps from them, so leaving them behind would resurrect the old
 * pattern on the next repaint. They go; the sounding steps go with them. */
static void ts_clear_notes(uint8_t t)
{
    ds_track *tr = &eng.pat.track[t];
    int i;
    for (i = 0; i < DS_STEPS; i++) {
        ds_step *st = &tr->steps[i];
        memset(st, 0, sizeof *st);
        memset(st->v, 0xFF, sizeof st->v);       /* note 0xFF = empty voice */
        st->prob = 100;                          /* a fresh step always plays */
        st->cond = DS_C_NONE;
    }
    tr->eu_on = 0;
    tr->eu_k  = 0;
    tr->eu_n  = 0;
    tr->eu_rot = 0;
    /* Nothing is done about notes that are sounding right now ON PURPOSE: each
     * one already has its gate-off scheduled in eng.gates[] (keyed by track, not
     * by the step that started it), so they end on time whether or not the step
     * that started them still exists. Panicking here would cut a note that was
     * going to end by itself in 20 ms, which is audible as a click. */
}

static void ts_prog_text(char *b)
{
    int32_t p = (int32_t)TRK()->prog;
    int k;
    if (p < 9)      *b++ = '0', *b++ = '0';
    else if (p < 99) *b++ = '0';
    fmt_int(b, p + 1);                           /* GM numbers are 1-based on paper */
    if (TRK()->channel == 9) {                   /* a drum track: program = kit */
        for (k = 0; k < GM_KIT_N; k++) {
            if (GM_KIT[k].prog != p)
                continue;
            strcpy_(txt_end(b), " ");
            strcpy_(txt_end(b), GM_KIT[k].name);
            return;
        }
        return;                                  /* a kit with no name: the number says it */
    }
    strcpy_(txt_end(b), " ");
    strcpy_(txt_end(b), GM_PROG[TRK()->prog & 127]);
}

static void draw_trkset(void)
{
    ds_track *tr = TRK();
    int i, y;

    cv_begin(240, 224, C_BLACK);
    {
        hdr_t h = {0};
        /* The context says what the channel MEANS (CH=10 is the drum channel).
         * MUTE/SOLO used to be appended to it ("MELODIC MUTE"), which failed at
         * exactly the job it had: same white text, so the silence read as part
         * of the channel's name (John 2026-10-30). It is a one-letter pill now:
         * the shape separates a state from a name, and the letter is the hero
         * column's own bare M / S, so the two screens agree without a legend.
         * One letter also makes the chip small, which was John's second note -
         * a word-sized pill took a third of the bar for two bits of state. */
        /* Page 1's context says what the channel MEANS. Page 2 has no channel
         * question to answer, so it says where in the feature you are: this is
         * the first page that needed to, and it is the model GLOBAL already set
         * ("GLOBAL 1/2"). */
        h.ctx = (char *)(ui_page[M_TRKSET]
                         ? "TRACK 2/2"
                         : (is_drum_trk() ? "DRUM" : "MELODIC"));
        h.right = (char *)0;
        h.right_c = TB_TX;
        h.dots = 1;
        h.page = 3;
        h.badge = tr->mute ? "M" : (tr->solo ? "S" : (char *)0);
        h.badge_c = HERO_MS;                     /* the hero column's M/S colour */
        ui_draw_header(&h);
    }
    for (i = 0; i < TS_NROW; i++) {
        char buf[36];
        char *v = buf;
        const char *val;
        y = TS_Y0 + i * TS_PITCH;
        if (i == (int)ts_row) {                  /* the row being edited */
            cv_rect(4, y - 6, 232, 21, RGB565(26, 26, 34));
            cv_rect(4, y - 6, 2, 21, C_AMB);
        }
        /* 16px, not 12 (John 2026-10-30: there was room and the page is read at
         * arm's length). FONT_S for the label, FONT_B for the value: same size,
         * different weight, so the column reads as label/value without a rule
         * between them. FONT_T stays the smallest type on the page, which is
         * the channel map's digits and the bottom line. */
        cv_text(12, y, &FONT_S, TS_LABEL[i], (i == (int)ts_row) ? C_AMB : C_GRAY);
        if (i == TS_CH) {
            fmt_int(v, (int32_t)tr->channel + 1);
            val = v;
        } else if (i == TS_PROG) {
            ts_prog_text(buf);
            val = buf;
        } else if (i == TS_ROUTE) {
            val = TS_ROUTE_NAME[tr->route % 3];
        } else if (i == TS_VOICE) {
            val = (tr->voice_mode == DS_VOICE_MONO) ? "MONO" : "POLY";
        } else {
            fmt_int(v, tr->swing_amt);
            strcpy_(txt_end(v), "%");
            val = v;
        }
        cv_text(100, y, &FONT_B, val, BRIGHTTX);
        if (i == TS_SWING && !eng.global_swing) {
            /* Per-track swing is a WEIGHT (global x track x 0.5): with the global
             * at zero this percentage produces nothing, and without a word the
             * row reads as a broken control. The global now lives on its own
             * page, so the hint also says where to go. */
            int w = cv_text_w(&FONT_B, val);
            cv_text(100 + w + 8, y, &FONT_S, "X GLOBAL 0", RGB565(150, 104, 48));
        }
    }
    /* ---- the channel map: who else lives on each channel ---- */
    cv_text(8, 144, &FONT_T, "CHANNELS IN USE", C_GRAY);
    {
        int c;
        for (c = 0; c < 16; c++) {
            int x = 8 + c * 14, t, users = 0;
            uint8_t other[DS_TRACKS], no = 0;
            char *n = txt_next();
            for (t = 0; t < DS_TRACKS; t++) {
                if (eng.pat.track[t].channel != c)
                    continue;
                users++;
                if (t != (int)sel_track)
                    other[no++] = (uint8_t)t;
            }
            if (c == (int)tr->channel) {         /* this track's own channel */
                cv_rect(x, 158, 12, 20, TB_TX);
                fmt_int(n, c + 1);
                cv_text(x + ((c >= 9) ? 1 : 3), 164, &FONT_T, n, TB_BG);
            } else if (users) {                  /* taken by someone else: bright digits,
                                                  * but a hollow cell. A filled cell would
                                                  * read as "mine" at a glance (both were
                                                  * RGB 58-ish and indistinguishable in a
                                                  * pixel scan), so the fill is reserved
                                                  * for the track being edited. */
                cv_rect(x, 158, 12, 1, RGB565(88, 88, 100));
                cv_rect(x, 177, 12, 1, RGB565(88, 88, 100));
                cv_rect(x, 158, 1, 20, RGB565(88, 88, 100));
                cv_rect(x + 11, 158, 1, 20, RGB565(88, 88, 100));
                fmt_int(n, c + 1);
                cv_text(x + ((c >= 9) ? 1 : 3), 164, &FONT_T, n, RGB565(214, 214, 224));
            } else {                             /* free: outline only, dim digits */
                cv_rect(x, 158, 12, 1, RGB565(40, 40, 50));
                cv_rect(x, 177, 12, 1, RGB565(40, 40, 50));
                cv_rect(x, 158, 1, 20, RGB565(40, 40, 50));
                cv_rect(x + 11, 158, 1, 20, RGB565(40, 40, 50));
                fmt_int(n, c + 1);
                cv_text(x + ((c >= 9) ? 1 : 3), 164, &FONT_T, n, RGB565(74, 74, 82));
            }
            if (c == (int)tr->channel) {         /* and the sharing line below */
                /* A local buffer, not txt_pool: this line can name up to 15
                 * tracks, and txt_pool's entries are 36 bytes. Writing it there
                 * overran pool[7] into jam_menu (50), which silently opened the
                 * SAVE menu and swallowed K1/K2. The list is capped at three
                 * names anyway: the panel fits 46 characters, and "who else is
                 * here" is answered by the cell being lit. */
                char s[48];
                /* Who decides this channel's program matters more than the roll
                 * call: on a shared channel only the owner's PROG reaches the
                 * receiver, and without this line the other tracks' knobs turn
                 * in silence and read as broken. The owner is the lowest track
                 * with notes, so "T7 SETS THE SOUND" is also the answer to
                 * "why does my PROG do nothing". */
                int own = ds_channel_owner(&eng, (int)tr->channel);
                /* The bottom line answers one question only: whose PROG counts
                 * here. When this track's does, the line stays what it always
                 * was (the roll call) - saying "you set the sound" would just
                 * repeat the filled channel cell. When it does not, that fact is
                 * the only thing on the page the player cannot discover by
                 * turning a knob, so it takes the line. */
                if (own != 0xFF && own != (int)sel_track) {
                    if (!ds_track_has_notes(&eng, (int)sel_track))
                        strcpy_(s, "EMPTY, T");
                    else
                        strcpy_(s, "T");
                    fmt_int(txt_end(s), (int32_t)own + 1);
                    strcpy_(txt_end(s), " SETS THE SOUND");
                } else if (!no) {
                    strcpy_(s, "ALONE ON THIS CHANNEL");
                } else {
                    /* Sharing a channel with placeholders is not sharing it: the
                     * eleven never-recorded tracks on ch0 hold no program and no
                     * notes, so the roll call would be noise. Only tracks that
                     * can actually sound are worth naming. */
                    uint8_t live = 0;
                    for (t = 0; t < no; t++)
                        if (ds_track_has_notes(&eng, (int)other[t])) live++;
                    int shown = live > 3 ? 3 : live;
                    if (!live) {
                        /* Empty tracks share the channel on paper only. The
                         * question this line answers is "who else sounds here",
                         * and the answer is nobody - not "11 empty slots". */
                        strcpy_(s, "NO OTHER TRACK SOUNDS");
                    } else {
                    strcpy_(s, "SHARED WITH");     /* the names carry their own T */
                    for (t = 0; t < no && shown; t++) {
                        if (!ds_track_has_notes(&eng, (int)other[t])) continue;
                        strcpy_(txt_end(s), " T");
                        fmt_int(txt_end(s), (int32_t)other[t] + 1);
                        shown--;
                    }
                    if (live > 3) {
                        strcpy_(txt_end(s), " +");
                        fmt_int(txt_end(s), (int32_t)(live - 3));
                    }
                    }
                }
                cv_text(8, 186, &FONT_T, s, no ? YELLOW : C_GRAY);
            }
        }
    }
    cv_blit_band(0, 0, 216);
    ui_draw_footer();
}

/* ---- page 2: the actions ---------------------------------------------------
 *
 * Two rows, big. The page exists because page 1 is full (five identity rows and
 * the channel map own y30..194), not because these two are lesser - so they get
 * page 1's row geometry exactly: same 22 px pitch, same 16 px label/value, same
 * amber rule on the row being edited. A second page that looked like a
 * different page would read as a different screen, not as the other half of
 * this one.
 */
static void draw_trkset2(void)
{
    ds_track *tr = TRK();
    int i, y;

    cv_begin(240, 224, C_BLACK);
    {
        hdr_t h = {0};
        h.ctx = (char *)"TRACK 2/2";
        h.right = (char *)0;
        h.right_c = TB_TX;
        h.dots = 1;
        h.page = 3;                              /* the TRACK SET dot, both pages */
        h.badge = tr->mute ? "M" : (tr->solo ? "S" : (char *)0);
        h.badge_c = HERO_MS;
        ui_draw_header(&h);
    }

    for (i = 0; i < TS2_NROW; i++) {
        char buf[36];
        const char *val;
        y = TS_Y0 + i * TS_PITCH;
        if (i == (int)ts_row) {
            cv_rect(4, y - 6, 232, 21, RGB565(26, 26, 34));
            cv_rect(4, y - 6, 2, 21, C_AMB);
        }
        cv_text(12, y, &FONT_S, TS2_LABEL[i], (i == (int)ts_row) ? C_AMB : C_GRAY);
        if (i == TS2_DIV) {
            /* The value is a word, not a number: "1/16T" is what the thing is
             * called, and the grid screens' HOME bar prints the same string. */
            val = div_str(tr->division);
            cv_text(100, y, &FONT_B, val, BRIGHTTX);
            {
                int w = cv_text_w(&FONT_B, val);
                cv_text(100 + w + 8, y, &FONT_S, "STEPS", RGB565(96, 96, 110));
            }
        } else {
            /* CLEAR TRACK shows no value: it is an action, and its "value" is a
             * decision that happens in a popup. The row says what it would act
             * on instead, so the row is never a word waiting for a noun. */
            val = "PRESS K2 RIGHT";
            cv_text(100, y, &FONT_B, val, (i == (int)ts_row) ? C_AMB : RGB565(120, 120, 134));
        }
        (void)buf;
    }

    /* Below the rows: what CLEAR TRACK would take, counted, and what it would
     * leave. Both belong HERE and not in the popup - the doubt ("will this move
     * my track to ch1?") arises at the row, and the popup's job is the decision,
     * not the lesson. One statement of the rule, at the place it is needed. */
    {
        int n = ts_notes_of(sel_track);
        char b[48];
        strcpy_(b, "STEPS IN USE ");
        fmt_int(txt_end(b), n);
        strcpy_(txt_end(b), " OF ");
        fmt_int(txt_end(b), DS_STEPS);
        cv_text(8, 100, &FONT_T, b, n ? YELLOW : RGB565(74, 74, 82));

        b[0] = 0;
        strcpy_(b, "WINDOW ");
        fmt_int(txt_end(b), (int32_t)tr->loop_start + 1);
        strcpy_(txt_end(b), "-");
        fmt_int(txt_end(b), (int32_t)((tr->loop_start + tr->loop_len - 1) % 64) + 1);
        strcpy_(txt_end(b), "  LEN ");
        fmt_int(txt_end(b), tr->loop_len);
        cv_text(8, 116, &FONT_T, b, RGB565(120, 120, 134));
    }
    /* the rule, stated once. John's line: a track clear must not quietly move the
     * track to ch1 / prog0 - so the survivors are named where the finger is. */
    cv_text(8, 152, &FONT_S, "CLEAR TAKES THE NOTES", RGB565(150, 104, 48));
    cv_text(8, 172, &FONT_T, "CH PROG DIV ROUTE VOICE", RGB565(120, 120, 134));
    cv_text(8, 188, &FONT_T, "SWING ARE KEPT", RGB565(120, 120, 134));

    cv_blit_band(0, 0, 216);
    ui_draw_footer();
}
