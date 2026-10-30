/* SPDX-License-Identifier: MPL-2.0
 * DriftSeq: the PRESET knob.
 *
 * PRESETS is the panel's seventh encoder (EN_PRESET, the one between ALGO and
 * K1) and it did nothing: ui_encoder returned on EN_ALGO || EN_PRESET with a
 * note that the batch was not decided. John 2026-10-30: "现在的PRESETS旋钮还没有
 * 用上，这个旋钮应该是立刻选择当前track的音色的，旋转的时候屏幕要有提示，可以采取
 * 类似popup的样式，字体大一点，停止旋转后等待几秒popup关闭。可以用GLO+PRESETS加速".
 *
 * So: it picks the CURRENT track's program, it takes effect on the detent (PC
 * goes out at once, see ds_send_prog - the keys have to audition the voice you
 * are choosing while the transport is stopped), and the screen says which voice
 * that is in FONT_M for PRESET_TTL frames after the last detent.
 *
 * Program change lands on a CHANNEL, so a drum track is a kit, not an
 * instrument; the popup says KIT there, the way TRACK SET's PROGRAM row does.
 * A track parked on the drum channel with a melodic program would otherwise
 * read "KIT 039 SYNTH BASS 1", which is a lie about both.
 */
#define PRESET_TTL 150                   /* frames: 2.5 s at the app's 60 fps */
#define PZ_STEP 1                        /* one program a detent... */
#define PZ_FAST 8                        /* ...and eight with GLO down */
/* The popup's own words, into the caller's buffer: "039 SYNTH BASS 1", or
 * "025 ELECTRONIC KIT" on the drum channel. The number is 1-based on paper, as
 * on TRACK SET's PROGRAM row (GM numbers 1..128, the wire carries 0..127). */
static void preset_text(char *b, uint8_t prog, uint8_t is_kit)
{
    int32_t p = (int32_t)prog;
    int k;
    if (p < 9)      *b++ = '0', *b++ = '0';
    else if (p < 99) *b++ = '0';
    fmt_int(b, p + 1);
    b = txt_end(b);
    *b++ = ' ';
    *b = 0;
    if (is_kit) {
        for (k = 0; k < GM_KIT_N; k++) {
            if (GM_KIT[k].prog != prog)
                continue;
            strcpy_(txt_end(b), GM_KIT[k].name);
            return;
        }
        strcpy_(txt_end(b), "KIT");         /* a kit with no name: the number says it */
        return;
    }
    strcpy_(txt_end(b), GM_PROG[prog & 127]);
}

/* One detent = one program (PZ_FAST with GLO down), dead stop at 0 and 127, and
 * every detent re-arms the popup so it closes only after the turning stops. */
static void preset_encoder(uint8_t enc, int8_t det)
{
    ds_track *tr = &eng.pat.track[sel_track & 15u];
    int step = (det > 0 ? 1 : -1) * (glo_held ? PZ_FAST : PZ_STEP);
    int v = (int)tr->prog + step;
    if (enc != EN_PRESET)
        return;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    if (v != (int)tr->prog) {
        tr->prog = (uint8_t)v;
        ds_send_prog(&eng, sel_track);       /* the keys audition it now */
    }
    pz_trk = sel_track;
    pz_prog = tr->prog;
    pz_ttl = PRESET_TTL;
}

/* Frames the popup has left: the UI's draw loop asks before it paints a page,
 * because a popup that is up replaces the page rather than sitting on it. */
static uint16_t preset_ticks(void) { return pz_ttl; }

/* probes: what the popup says, and how wide that is (the 240 px check) */
static void preset_popup_text(char *b)
{
    ds_track *tr = &eng.pat.track[sel_track & 15u];
    preset_text(b, tr->prog, (tr->channel == 9));
}

/* One program's name, 1-based number included, into b (24 B). Shared by the
 * popup's three lines so they cannot disagree about the padding. */
static void preset_name(char *b, uint8_t prog, uint8_t is_kit)
{
    preset_text(b, prog, is_kit);
}

static void preset_draw(void)
{
    ds_track *tr = &eng.pat.track[sel_track & 15u];
    char b[24], pv[24], nx[24];
    char *h = txt_next();
    int is_kit = (tr->channel == 9);
    int32_t w;
    int x, lo = (int)tr->prog > 0, hi = (int)tr->prog < 127;

    preset_name(b, tr->prog, is_kit);
    if (lo) preset_name(pv, (uint8_t)(tr->prog - 1), is_kit);
    if (hi) preset_name(nx, (uint8_t)(tr->prog + 1), is_kit);

    /* The canvas is 240x224 and the footer owns the last 24 rows: the popup
     * paints 0..215 and then paints its own footer, because the panel still works
     * while it is up. */
    cv_begin(240, 216, RGB565(16, 16, 21));

    /* "TRACK 4" and the channel, not "4 VOICE": the first draft read as four
     * voices of polyphony, which is a different fact entirely. */
    strcpy_(h, "TRACK ");
    fmt_int(txt_end(h), (int32_t)sel_track + 1);
    cv_text(8, 6, &FONT_T, h, C_AMB);
    h = txt_next();
    if (is_kit)
        strcpy_(h, "DRUM CH 10");
    else {
        strcpy_(h, "CH ");
        fmt_int(txt_end(h), (int32_t)tr->channel + 1);
    }
    cv_text(240 - 8 - (int)cv_text_w(&FONT_T, h), 6, &FONT_T, h, RGB565(96, 96, 110));

    /* Three lines: the neighbours greyed, the chosen one in FONT_M and centred.
     * The neighbours are why this is not a duplicate of the name - they say where
     * in the bank the knob is, which the name alone cannot, and they make the
     * popup readable while the knob is still turning. */
    if (lo) {
        w = cv_text_w(&FONT_T, pv);
        cv_text((240 - (int)w) / 2, 30, &FONT_T, pv, RGB565(84, 84, 98));
    }
    if (hi) {
        w = cv_text_w(&FONT_T, nx);
        cv_text((240 - (int)w) / 2, 76, &FONT_T, nx, RGB565(84, 84, 98));
    }
    w = cv_text_w(&FONT_M, b);
    x = (240 - (int)w) / 2;
    if (x < 6) x = 6;
    cv_text(x, 46, &FONT_M, b, BRIGHTTX);

    /* the ends of the bank are worth saying out loud: a dead stop that looks like
     * a dead knob is the difference between "this is 001" and "the knob broke" */
    cv_text(8, 190, &FONT_T, lo ? (hi ? "" : "END OF BANK") : "START OF BANK",
            RGB565(96, 96, 110));
    cv_blit(0, 0);

    /* its own footer: which knob is doing this, and the acceleration with GLO */
    cv_begin(240, 24, TB_BG);
    cv_text(8, 6, &FONT_T, "PRESET", FOOT_GRAY);
    cv_text(120, 6, &FONT_T, "GLO: 8X", FOOT_GRAY);
    cv_blit(0, 216);
}
