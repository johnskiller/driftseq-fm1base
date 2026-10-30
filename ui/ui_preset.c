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

/* ---- drum tracks: the nine kits, not 128 numbers ----
 *
 * John 2026-10-30: "我建议drum的preset就用有值的那9个". A program change on the
 * drum channel selects a KIT, and GM2 names exactly nine of them; the other 119
 * numbers are unnamed slots that every receiver maps back to its standard kit
 * (measured on the built-in EAS: prog 1/8/16/25/56/127 all render identical
 * audio). Stepping through them is 119 detents of nothing.
 *
 * So on a drum track the knob walks GM_KIT and nothing else. TRACK SET's PROGRAM
 * row keeps the full 0..127: it is the honest editor, and an external module may
 * well have kits sitting in the unnamed slots. PRESETS is the performance
 * control, and a performance control should only stop where something is.
 */
static int kit_slot(uint8_t prog)         /* -1 if this program is not a named kit */
{
    int k;
    for (k = 0; k < GM_KIT_N; k++)
        if (GM_KIT[k].prog == prog)
            return k;
    return -1;
}

/* The kit `n` detents away in direction d, dead stop at both ends. A program
 * parked on an unnamed slot (an external editor can put one there) snaps into
 * the set in the direction of the turn instead of dead-stopping on nothing. */
static int kit_land(uint8_t prog, int d, int n)
{
    int k = kit_slot(prog), i;

    if (k >= 0) {
        k += d * n;
        if (k < 0) k = 0;
        if (k >= GM_KIT_N) k = GM_KIT_N - 1;
        return (int)GM_KIT[k].prog;
    }
    /* off the set: first kit past it, then n-1 more slots */
    k = (d > 0) ? GM_KIT_N : -1;
    for (i = 0; i < GM_KIT_N; i++)
        if ((d > 0) ? (GM_KIT[i].prog > prog) : (GM_KIT[i].prog < prog)) {
            k = i;
            break;
        }
    if (d > 0) {
        if (k >= GM_KIT_N) k = GM_KIT_N - 1;
        k += n - 1;
        if (k >= GM_KIT_N) k = GM_KIT_N - 1;
    } else {
        if (k < 0) k = 0;
        k -= n - 1;
        if (k < 0) k = 0;
    }
    return (int)GM_KIT[k].prog;
}

/* One detent = one program (PZ_FAST with GLO down), dead stop at 0 and 127, and
 * every detent re-arms the popup so it closes only after the turning stops. */
static void preset_encoder(uint8_t enc, int8_t det)
{
    ds_track *tr = &eng.pat.track[sel_track & 15u];
    int step, v;

    if (enc != EN_PRESET)
        return;
    step = (det > 0 ? 1 : -1) * (glo_held ? PZ_FAST : PZ_STEP);
    v = (int)tr->prog;
    if (tr->channel == 9) {
        /* GM2 names nine kits; the other 119 program numbers are unnamed slots
         * that every receiver maps back to its standard kit (measured on the
         * built-in EAS: prog 1/8/16/25/56/127 all render identical audio).
         * Stepping through them is 119 detents of nothing, so the knob walks the
         * nine and nothing else. TRACK SET's PROGRAM row keeps all 128 - it is
         * the honest editor, and a module may well have kits in the unnamed
         * slots. */
        int n = (step < 0 ? -step : step);
        v = kit_land(tr->prog, step > 0 ? 1 : -1, n > 0 ? n : 1);
    } else {
        v += step;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
    }
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
    int x, lo, hi;

    /* The neighbours are the same nine kits on a drum track, so they come from
     * the same walk the knob uses - prog-1 there would print "023 KIT" above
     * "024 ELECTRONIC KIT", a number that is not a kit and is not where you
     * came from either. */
    if (is_kit) {
        lo = kit_land(tr->prog, -1, 1) != (int)tr->prog;
        hi = kit_land(tr->prog, +1, 1) != (int)tr->prog;
    } else {
        lo = (int)tr->prog > 0;
        hi = (int)tr->prog < 127;
    }
    preset_name(b, tr->prog, is_kit);
    if (lo)
        preset_name(pv, (uint8_t)(is_kit ? kit_land(tr->prog, -1, 1)
                                         : (int)tr->prog - 1), is_kit);
    if (hi)
        preset_name(nx, (uint8_t)(is_kit ? kit_land(tr->prog, +1, 1)
                                         : (int)tr->prog + 1), is_kit);

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

/* ---- SELECT: the voice follows the track ----
 *
 * SELECT moves which track the panel is talking about, and on a page whose keys
 * audition, that means the keys change instrument. But the receiver keeps ONE
 * program per channel, and the tick path only re-states it while the transport
 * runs - so stopped on TRACK SET, switching to a track with a different program
 * left the synth on the previous track's voice: the row said "041 VIOLIN" and
 * the keys still played the bass (John 2026-10-30: "track set画面，chang prg后的
 * 确起作用了，但是此时SELECT切换track，音色又不对了").
 *
 * One place rather than three: every assignment to sel_track (TRACK SET's
 * SELECT, the bare-page SELECT, NEW TRK in the jam menu) goes through here, so a
 * future track selector cannot forget. Boot sets sel_track = 0 directly, before
 * there is a transport to state anything to.
 */
static void ui_select_track(uint8_t t)
{
    sel_track = t;
    ds_send_prog(&eng, sel_track);
}
