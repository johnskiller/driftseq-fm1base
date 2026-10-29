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
 */
static uint8_t ts_row;          /* 0..TS_NROW-1, dead-stop */

#define TS_NROW 5
#define TS_CH   0
#define TS_PROG 1
#define TS_ROUTE 2
#define TS_VOICE 3
#define TS_SWING 4

static const char *const TS_LABEL[TS_NROW] = {"CHANNEL", "PROGRAM", "ROUTE", "VOICE", "SWING"};

/* GM Level 1 program names, abbreviated to what fits (FONT_T is caps-only, and
 * the value column starts at x=96: 17 characters is the budget). Kept in the
 * file that uses them — nothing else draws a program name. */
static const char *const GM_PROG[128] = {
    /*   0-  7 piano */      "ACOUSTIC GRAND", "BRIGHT PIANO", "ELECTRIC GRAND", "HONKY-TONK",
                             "ELECTRIC PIANO", "E.PIANO 2", "HARPSICHORD", "CLAVINET",
    /*   8- 15 chromatic */  "CELESTA", "GLOCKENSPIEL", "MUSIC BOX", "VIBRAPHONE",
                             "MARIMBA", "XYLOPHONE", "TUBULAR BELLS", "DULCIMER",
    /*  16- 23 organ */      "DRAWBAR ORGAN", "PERCUSSIVE ORG", "ROCK ORGAN", "CHURCH ORGAN",
                             "REED ORGAN", "ACCORDION", "HARMONICA", "TANGO ACCORDION",
    /*  24- 31 guitar */     "NYLON GUITAR", "STEEL GUITAR", "JAZZ GUITAR", "CLEAN GUITAR",
                             "MUTED GUITAR", "OVERDRIVE GT", "DISTORTION GT", "GT HARMONICS",
    /*  32- 39 bass */       "ACOUSTIC BASS", "FINGERED BASS", "PICKED BASS", "FRETLESS BASS",
                             "SLAP BASS 1", "SLAP BASS 2", "SYNTH BASS 1", "SYNTH BASS 2",
    /*  40- 47 strings */    "VIOLIN", "VIOLA", "CELLO", "CONTRABASS",
                             "TREMOLO STRINGS", "PIZZICATO", "ORCH HARP", "TIMPANI",
    /*  48- 55 ensemble */   "STRING ENSEMBLE", "STRINGS 2", "SYNTH STRINGS", "SYNTH STR 2",
                             "CHOIR AAHS", "VOICE OOHS", "SYNTH VOICE", "ORCHESTRA HIT",
    /*  56- 63 brass */      "TRUMPET", "TROMBONE", "TUBA", "MUTED TRUMPET",
                             "FRENCH HORN", "BRASS SECTION", "SYNTH BRASS 1", "SYNTH BRASS 2",
    /*  64- 71 reed */       "SOPRANO SAX", "ALTO SAX", "TENOR SAX", "BARITONE SAX",
                             "OBOE", "ENGLISH HORN", "BASSOON", "CLARINET",
    /*  72- 79 pipe */       "PICCOLO", "FLUTE", "RECORDER", "PAN FLUTE",
                             "BLOWN BOTTLE", "SHAKUHACHI", "WHISTLE", "OCARINA",
    /*  80- 87 synth lead */ "SQUARE LEAD", "SAWTOOTH LEAD", "CALLIOPE LEAD", "CHIFF LEAD",
                             "CHARANG LEAD", "VOICE LEAD", "FIFTHS LEAD", "BASS + LEAD",
    /*  88- 95 synth pad */  "NEW AGE PAD", "WARM PAD", "POLYSYNTH PAD", "CHOIR PAD",
                             "BOWED PAD", "METALLIC PAD", "HALO PAD", "SWEEP PAD",
    /*  96-103 synth fx */   "RAIN FX", "SOUNDTRACK FX", "CRYSTAL FX", "ATMOSPHERE FX",
                             "BRIGHTNESS FX", "GOBLINS FX", "ECHOES FX", "SCI-FI FX",
    /* 104-111 ethnic */     "SITAR", "BANJO", "SHAMISEN", "KOTO",
                             "KALIMBA", "BAG PIPE", "FIDDLE", "SHANAI",
    /* 112-119 percussive */ "TINKLE BELL", "AGOGO", "STEEL DRUMS", "WOODBLOCK",
                             "TAIKO DRUM", "MELODIC TOM", "SYNTH DRUM", "REV CYMBAL",
    /* 120-127 sound fx */   "FRET NOISE", "BREATH NOISE", "SEASHORE", "BIRD TWEET",
                             "TELEPHONE", "HELICOPTER", "APPLAUSE", "GUNSHOT"
};

static const char *const TS_ROUTE_NAME[3] = {"USB", "GM", "USB+GM"};

/* A CH or PROG change must re-send the program change, or the new sound only
 * arrives at the next PLAY: emit_pc runs once per track per run (pc_sent).
 * Clearing the flag makes the engine's next tick send it, which is what "I
 * turned the knob and heard it change" requires. */
static void ts_resend_pc(void)
{
    eng.pc_sent[sel_track] = 0;
}

static void trkset_encoder(uint8_t enc, int8_t det)
{
    ds_track *tr = TRK();

    if (enc == EN_SELECT) {                      /* the page is per-track: SELECT walks tracks,
                                                  * dead stop at both ends like every other
                                                  * track selector on the panel */
        int v = (int)sel_track + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > DS_TRACKS - 1) v = DS_TRACKS - 1;
        sel_track = (uint8_t)v;
        ts_row = 0;                              /* a new track starts at the top */
        return;
    }
    if (enc == EN_K1) {                          /* row: dead stop, no wrap */
        int v = (int)ts_row + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > TS_NROW - 1) v = TS_NROW - 1;
        ts_row = (uint8_t)v;
        return;
    }
    if (enc == EN_K4) {                          /* GLOBAL swing, always live */
        int v = (int)eng.global_swing + det * 5;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        eng.global_swing = (uint8_t)v;
        return;
    }
    if (enc == EN_K3) {                          /* MUTE/SOLO, as everywhere */
        k3_state_slider(tr, sel_track, det);
        return;
    }
    if (enc != EN_K2)
        return;
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
            ts_resend_pc();
        }
        break;
    }
    case TS_PROG: {
        int v = (int)tr->prog + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        if (v != (int)tr->prog) {
            tr->prog = (uint8_t)v;
            ts_resend_pc();
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
    case TS_SWING: {
        int v = (int)tr->swing_amt + det * 5;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        tr->swing_amt = (uint8_t)v;
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
        ui_mode = (ui_mode == M_TRKSET) ? M_HOME : M_TRKSET;
        ts_row = 0;
        focus_layer = 0;
        return 1;
    }
    if (ui_mode != M_TRKSET)
        return 0;
    if (btn == B_HOME) {
        ui_mode = M_HOME;
        return 1;                                /* consumed: the generic B_HOME case
                                                  * would otherwise toggle HOME->GRID
                                                  * on the mode we just cleared. SEQ and
                                                  * ARP fall through to their own cases:
                                                  * they already set the page you asked
                                                  * for. */
    }
    return 0;
}

/* "039 SYNTH BASS 1" into b — the buffer is the caller's, so this does not
 * juggle the text ring (that ring is 8 deep and the page already uses several). */
static void ts_prog_text(char *b)
{
    int32_t p = (int32_t)TRK()->prog;
    if (p < 9)      *b++ = '0', *b++ = '0';
    else if (p < 99) *b++ = '0';
    fmt_int(b, p + 1);                           /* GM numbers are 1-based on paper */
    strcpy_(txt_end(b), " ");
    strcpy_(txt_end(b), GM_PROG[TRK()->prog & 127]);
}

static void draw_trkset(void)
{
    ds_track *tr = TRK();
    int i, y;

    cv_begin(240, 224, C_BLACK);
    {
        hdr_t h;
        /* The context says what the channel MEANS (CH=10 is the drum channel),
         * and appends MUTE/SOLO when that is why the track is silent — on a page
         * that edits what a track sounds like, "why don't I hear it" has to be
         * answerable without leaving. */
        static const char *const CTX_DRUM[3] = {"DRUM", "DRUM MUTE", "DRUM SOLO"};
        static const char *const CTX_MEL[3]  = {"MELODIC", "MELODIC MUTE", "MELODIC SOLO"};
        uint8_t st = tr->mute ? 1 : (tr->solo ? 2 : 0);
        h.ctx = (char *)(is_drum_trk() ? CTX_DRUM[st] : CTX_MEL[st]);
        h.right = (char *)0;
        h.right_c = TB_TX;
        h.dots = 1;
        h.page = 3;
        ui_draw_header(&h);
    }
    for (i = 0; i < TS_NROW; i++) {
        char buf[36];
        char *v = buf;
        const char *val;
        y = 34 + i * 20;
        if (i == (int)ts_row) {                  /* the row being edited */
            cv_rect(4, y - 5, 232, 18, RGB565(26, 26, 34));
            cv_rect(4, y - 5, 2, 18, C_AMB);
        }
        cv_text(12, y, &FONT_T, TS_LABEL[i], (i == (int)ts_row) ? C_AMB : C_GRAY);
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
        cv_text(96, y, &FONT_T, val, BRIGHTTX);
    }
    /* ---- the channel map: who else lives on each channel ---- */
    cv_text(8, 138, &FONT_T, "CHANNELS IN USE", C_GRAY);
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
                cv_rect(x, 152, 12, 20, TB_TX);
                fmt_int(n, c + 1);
                cv_text(x + ((c >= 9) ? 1 : 3), 158, &FONT_T, n, TB_BG);
            } else if (users) {                  /* taken by someone else: bright digits,
                                                  * but a hollow cell. A filled cell would
                                                  * read as "mine" at a glance (both were
                                                  * RGB 58-ish and indistinguishable in a
                                                  * pixel scan), so the fill is reserved
                                                  * for the track being edited. */
                cv_rect(x, 152, 12, 1, RGB565(88, 88, 100));
                cv_rect(x, 171, 12, 1, RGB565(88, 88, 100));
                cv_rect(x, 152, 1, 20, RGB565(88, 88, 100));
                cv_rect(x + 11, 152, 1, 20, RGB565(88, 88, 100));
                fmt_int(n, c + 1);
                cv_text(x + ((c >= 9) ? 1 : 3), 158, &FONT_T, n, RGB565(214, 214, 224));
            } else {                             /* free: outline only, dim digits */
                cv_rect(x, 152, 12, 1, RGB565(40, 40, 50));
                cv_rect(x, 171, 12, 1, RGB565(40, 40, 50));
                cv_rect(x, 152, 1, 20, RGB565(40, 40, 50));
                cv_rect(x + 11, 152, 1, 20, RGB565(40, 40, 50));
                fmt_int(n, c + 1);
                cv_text(x + ((c >= 9) ? 1 : 3), 158, &FONT_T, n, RGB565(74, 74, 82));
            }
            if (c == (int)tr->channel) {         /* and the sharing line below */
                /* A local buffer, not txt_pool: this line can name up to 15
                 * tracks, and txt_pool's entries are 36 bytes. Writing it there
                 * overran pool[7] into jam_menu (50), which silently opened the
                 * SAVE menu and swallowed K1/K2. The list is capped at three
                 * names anyway: the panel fits 46 characters, and "who else is
                 * here" is answered by the cell being lit. */
                char s[48];
                char *p = s;
                if (!no) {
                    strcpy_(p, "ALONE ON THIS CHANNEL");
                } else {
                    uint8_t shown = no > 3 ? 3 : no;
                    strcpy_(p, "SHARED WITH");    /* the names carry their own T */
                    for (t = 0; t < shown; t++) {
                        strcpy_(txt_end(s), " T");
                        fmt_int(txt_end(s), (int32_t)other[t] + 1);
                    }
                    if (no > shown) {
                        strcpy_(txt_end(s), " +");
                        fmt_int(txt_end(s), (int32_t)(no - shown));
                    }
                }
                cv_text(8, 180, &FONT_T, s, no ? YELLOW : C_GRAY);
            }
        }
    }
    cv_blit_band(0, 0, 216);
    ui_draw_footer();
}
