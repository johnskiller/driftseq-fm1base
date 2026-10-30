/* ui_screens.c — screen painters (part of ui.c unity section).
 * Faithful port of web/emu.js r16j (f33931a) draw branches: same geometry,
 * same colors, same layering. 240x240 budget: top bar 16 + main + footer 24.
 * Text via ui_text.c builders (no printf in the platform).
 * Font mapping (real Barlow roles): FONT_T 12px caps for titles/gutters/
 * footer; FONT_XS 15px for readout/cards. r16j's 11px monospace -> XS. */

/* r16j top-bar color constants (all screens share) */
#define TB_BG      RGB565(24, 24, 32)
#define TB_TX      RGB565(217, 217, 232)
#define GREEN_OK   RGB565(95, 224, 138)
#define MUTE_GRAY  RGB565(90, 90, 98)
#define FOOT_GRAY  RGB565(138, 138, 153)
#define YELLOW     RGB565(230, 200, 74)
#define WARMWHITE  RGB565(255, 244, 214)
#define BRIGHTTX   RGB565(240, 240, 246)

/* hsl(note%12*30, 72%, 55%) — the 12-hue pitch wheel (melodic line color) */
static uint16_t hue12(uint8_t note)
{
    static const uint16_t H12[12] = {
        RGB565(217, 74, 61),  RGB565(217, 128, 51), RGB565(217, 189, 46),
        RGB565(151, 214, 57), RGB565(87, 212, 66),  RGB565(56, 209, 125),
        RGB565(51, 205, 189), RGB565(52, 177, 214), RGB565(58, 122, 216),
        RGB565(97, 77, 215),  RGB565(163, 62, 209), RGB565(212, 60, 180),
    };
    return H12[note % 12u];
}

/* sorted voices of step s, pitch-ascending -> out[0..3] (note, vel, gate).
 * sort_vs is DECLARED in ui.c (edit_key_handler consumes it before this
 * definition appears in the unity include order). */
static uint8_t sort_n;
/* ---- a tie's pitch is not its own ----
 * A tie step carries no attack. The note that sounds through it belongs to the
 * previous attacking step, so that is the pitch the cell must draw -- drawing
 * the tie step's own stored note (it may still hold one, from before it was
 * tied) would claim a pitch that nobody hears.
 *
 * "Previous attacking step" walks backwards within the loop window and wraps,
 * skipping tie steps. Two ties in a row both resolve to the same attack, which
 * is what makes a tied run read as one long note. Returns -1 when the walk
 * finds no attack at all (an orphan tie: the loop is entirely ties), in which
 * case the cell draws no pitch line. */
static int prev_attacking_step(const ds_track *t, uint8_t s)
{
    uint32_t start = t->loop_start, len = t->loop_len, rel, d;
    if (!len) return -1;
    rel = (s + 64u - start) & 63u;
    if (rel >= len) return -1;                       /* outside the window */
    for (d = 1; d < len; d++) {
        uint32_t p = (start + rel + 64u - d) & 63u;   /* walk back, wrapping */
        const ds_step *st = &t->steps[p];
        if (st->tie) continue;
        if (st->on && st->nvoices) return (int)p;
        return -1;                                   /* hit a non-tie that does not attack */
    }
    return -1;
}
/* the pitch a tie cell should draw: the lowest voice of the step it extends.
 * The engine holds every voice of an attack for the same gate, so the lowest is
 * the stable choice -- and it matches the line the attack itself drew lowest. */
static uint8_t tie_pitch_of(const ds_track *t, uint8_t s)
{
    const ds_step *st = &t->steps[s];
    uint8_t vi, best = 0xFF;
    for (vi = 0; vi < st->nvoices; vi++) {
        uint8_t n = st->v[vi].note;
        if (n == 0xFF) continue;
        if (best == 0xFF || n < best) best = n;
    }
    return best;
}

static void voices_sorted(uint8_t t, uint8_t s)
{
    uint8_t nv = eng.pat.track[t].steps[s].nvoices, i, j;
    sort_n = 0;
    for (i = 0; i < nv; i++) {
        ds_note *v = &eng.pat.track[t].steps[s].v[i];
        if (v->note == 0xFF)
            continue;
        sort_vs[sort_n][0] = v->note;
        sort_vs[sort_n][1] = v->vel;
        sort_vs[sort_n][2] = v->gate;
        sort_n++;
    }
    for (i = 0; i < sort_n; i++)                    /* insertion sort (n<=4) */
        for (j = (uint8_t)(i + 1); j < sort_n; j++)
            if (sort_vs[j][0] < sort_vs[i][0]) {
                uint8_t tmp0 = sort_vs[i][0], tmp1 = sort_vs[i][1], tmp2 = sort_vs[i][2];
                sort_vs[i][0] = sort_vs[j][0]; sort_vs[i][1] = sort_vs[j][1]; sort_vs[i][2] = sort_vs[j][2];
                sort_vs[j][0] = tmp0; sort_vs[j][1] = tmp1; sort_vs[j][2] = tmp2;
            }
}

/* note name "C#3" (FONT_T-safe; NOTE_NAMES from ui_glue.c) */
static char *note_name2(uint8_t note)
{
    char *b = txt_next();
    strcpy_(b, ui_note_name(note));
    fmt_int(txt_end(b), (int32_t)(note / 12u) - 1);
    return b;
}

/* gate display: 0xFF = INF (r16j shows the loop symbol) */
static char *gate_str(uint8_t gate)
{
    char *b = txt_next();
    if (gate == 0xFF) { b[0] = 'L'; b[1] = 'O'; b[2] = 0; return b; }  /* FONT_T-safe "∞" */
    fmt_int(b, (int32_t)gate);                       /* raw steps, r16j parity (g4 = 1.00) */
    return b;
}

/* chord name (r16j rev.13 verbatim): single note = "C3"; interval-set match
 * against common triads/7ths = "Cmaj7" style; else pitch classes spaced out.
 * Returns a txt_next() buffer ("" = empty step). */
static char *chord_name(uint8_t t, uint8_t s)
{
    char *b = txt_next();
    uint8_t i, j, root, ivs[4], niv = 0;
    uint16_t set_mask;                              /* 12 bits — uint8_t would eat bit8+ (Db..) */
    voices_sorted(t, s);
    b[0] = 0;
    if (sort_n == 0)
        return b;
    if (sort_n == 1) {
        strcpy_(b, note_name2(sort_vs[0][0]));
        return b;
    }
    root = (uint8_t)(sort_vs[0][0] % 12u);
    set_mask = 0;
    for (i = 0; i < sort_n; i++)
        set_mask = (uint16_t)(set_mask | (1u << (sort_vs[i][0] % 12u)));
    for (i = 0; i < 12; i++) {                      /* sorted interval list (dedup via mask) */
        if (set_mask & (1u << i)) {
            uint8_t iv = (uint8_t)((i - root + 12u) % 12u);
            for (j = 0; j < niv && ivs[j] < iv; j++)
                ;
            if (j == niv || ivs[j] != iv) {
                int k;
                for (k = niv; k > j; k--)
                    ivs[k] = ivs[k - 1];
                ivs[j] = iv;
                niv++;
            }
        }
    }
    {   /* match "0,4,7"=maj "0,3,7"=m "0,4,7,11"=maj7 "0,3,7,10"=m7
         * "0,3,6"=dim "0,4,8"=aug "0,5,7"=sus4 "0,2,7"=sus2; else spell out */
        const char *q = NULL;
        if (niv == 3 && ivs[0] == 0 && ivs[1] == 4 && ivs[2] == 7) q = "maj";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 3 && ivs[2] == 7) q = "m";
        else if (niv == 4 && ivs[0] == 0 && ivs[1] == 4 && ivs[2] == 7 && ivs[3] == 11) q = "maj7";
        else if (niv == 4 && ivs[0] == 0 && ivs[1] == 3 && ivs[2] == 7 && ivs[3] == 10) q = "m7";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 3 && ivs[2] == 6) q = "dim";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 4 && ivs[2] == 8) q = "aug";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 5 && ivs[2] == 7) q = "sus4";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 2 && ivs[2] == 7) q = "sus2";
        if (q) {
            strcpy_(b, ui_note_name(root));             /* r16j: NAMES[root], NO octave */
            strcpy_(txt_end(b), q);
        } else {
            for (i = 0; i < sort_n; i++) {
                strcpy_(txt_end(b), ui_note_name(sort_vs[i][0]));
                if (i + 1 < sort_n)
                    strcpy_(txt_end(b), " ");
            }
        }
    }
    return b;
}

/* ---- gate -> pixel run (plan D, John 2026-10-27) --------------------------
 * The melodic grid line's LENGTH now carries gate: the same primitive a tie
 * already uses ("the line runs through"), so a limited gate and a tie are one
 * rule -- the line runs for as long as the note sounds, in cells of the
 * track's division.
 *
 * The run mirrors the engine's land() arithmetic exactly (driftseq.c): a
 * limited gate is a pure DURATION -- the steps it crosses say nothing, only a
 * tie adds a cell (and only the run of ties immediately after the attack);
 * 0xFF walks forward until the next attacking step or the window end. Both cut
 * at the loop window, because land()'s span does. */
static int gate_run_cells(const ds_track *t, uint8_t s, uint8_t gate)
{
    uint32_t start = t->loop_start, len = t->loop_len, rel, d;
    int cells;
    if (!len) return 1;
    rel = (s + 64u - start) & 63u;
    if (rel >= len) return 1;
    if (gate == 0xFF) {
        uint32_t span = len - rel;
        cells = (int)span;
        for (d = 1; d < span; d++) {
            const ds_step *nx = &t->steps[(start + rel + d) & 63];
            if (nx->on && nx->nvoices) { cells = (int)d; break; }   /* retrigger */
        }
        return cells;
    }
    cells = (gate > len - rel) ? (int)(len - rel) : gate;   /* window cut */
    for (d = 1; ; d++) {
        const ds_step *nx = &t->steps[(start + rel + d) & 63];
        if ((int)rel + (int)d >= (int)len) break;           /* stay in the window */
        if (!nx->tie) break;
        cells++;                                            /* a tie extends the hold */
    }
    return cells;
}

/* effective gate for the EDIT panel: what the engine will DO with this row,
 * not what is stored. An attack row sounds its own gate plus the tie run
 * after it (gate_run_cells == the very length the grid line draws). A tied
 * row never attacks (land() returns early): the sounding hold belongs to the
 * nearest attack behind it -- prefer that attack's voice at the SAME pitch,
 * else its first voice. -1 = nothing owns this hold (a rest breaks it). */
static int gate_display(const ds_track *t, uint8_t s, uint8_t gate, uint8_t note)
{
    uint32_t start = t->loop_start, len = t->loop_len, rel, d;
    if (!len) return -1;
    rel = (s + 64u - start) & 63u;
    if (rel >= len) return -1;
    if (!t->steps[s].tie)
        return t->steps[s].on ? gate_run_cells(t, s, gate) : -1;
    for (d = 1; d <= rel; d++) {                /* nearest attack looking back */
        const ds_step *pv = &t->steps[(start + rel - d) & 63];
        uint8_t vi;
        if (pv->on && pv->nvoices) {
            for (vi = 0; vi < pv->nvoices; vi++)
                if (pv->v[vi].note == note)     /* the hold at THIS row's pitch */
                    return gate_run_cells(t, (uint8_t)((s + 64u - d) & 63u),
                                          pv->v[vi].gate);
            return gate_run_cells(t, (uint8_t)((s + 64u - d) & 63u), pv->v[0].gate);
        }
        if (!pv->tie) break;                    /* a rest breaks the hold */
    }
    return -1;
}

/* ---- "the knob you are turning says its value" -------------------------
 *
 * John: "每个旋钮改变时在旋钮位置显示value，停下来后恢复旋钮提示".
 *
 * The footer already prints one name per knob at fixed columns, so the value
 * goes where the name was: the eye is at the footer when the hand is at the
 * knob, and a readout that jumps to the header (where S1 used to live) makes
 * the player look away from the thing they are turning.
 *
 * Any detent from any encoder re-stamps the timer, and the footer falls back
 * to names when it expires. At 60 fps that is 0.65 s: long enough to read a
 * number, short enough that the footer is not stuck showing a value while the
 * player has moved on.
 *
 * Only K1..K4 have a footer column. SELECT/ALGO/PRESET have none, so they are
 * ignored rather than aliased onto a column that means something else.
 */
#define KN_FRAMES 40
static uint8_t  kn_i;        /* which knob (0..3 = K1..K4), valid if kn_ttl > 0 */
static uint8_t  kn_ttl;      /* frames left */

/* defined at the end of the file: it reads hero_bars, div_str and the per-mode
 * step fields, which are declared further down than the footer that calls it */
static const char *kn_value_text(void);

static void kn_poke(uint8_t enc)
{
    if (enc < EN_K1 || enc > EN_K4)
        return;                              /* no footer column for this one */
    kn_i = (uint8_t)(enc - EN_K1);
    kn_ttl = KN_FRAMES;
}

/* ---- the SAVE menu: MERGE / REPLACE / NEW TRK / CLEAR ----
 * Every SAVE press opens this footer menu (John: "save时最好可选：merge，
 * replace，new trk") — no default action, because a silent default is how a
 * take gets merged twice. K1..K4 rotate the highlight, SAVE confirms, HOME
 * closes, and 3 s of no input closes it (the footer is a readout, not a
 * resident layer). The engine call sits in jam_menu_act(); the menu itself
 * only moves a cursor so probes can assert on state, not pixels alone.
 * State + JAM_MENU_FRAMES live in ui_glue.c (declared before both callers). */
static const char *const JAM_MENU_LABELS[4] = {"MERGE", "REPLACE", "NEW TRK", "CLEAR"};

/* the menu's actions, in jam_menu_sel order. NEW TRK also selects the track
 * it landed on (the player's eye is already on the grid). */
static void jam_menu_act(uint8_t which)
{
    if (which == 3) {
        ds_jam_reset(&eng);
        app_say("JAM", "CLEARED");
        return;
    }
    if (which == 0) {
        ds_jam_save_merge(&eng, sel_track);
        app_say("JAM", "MERGED");
    } else if (which == 1) {
        app_say("JAM", ds_jam_save_replace(&eng, sel_track) == 1 ? "RING EMPTY" : "REPLACED");
    } else {
        uint8_t t;
        if (ds_jam_save_new(&eng, &t) == 0) {
            sel_track = t;
            app_say("JAM", txt_trk("TO ", (uint8_t)(t + 1)));
        } else {
            app_say("JAM", "NO FREE TRK");       /* ring survives (engine kept it) */
        }
    }
}

static void jam_menu_close(void) { jam_menu = 0; jam_menu_ttl = 0; }

/* ---- footer (prefix-free, fixed columns, 10px) ----
 * The footer is the contract "手不用看旋钮就知道在改什么" (03 §0), so the GLO
 * layer gets its own row of names: while GLO is held the four big boxes are
 * no longer STEP/LEN/MUTE/PROB, and showing the bare names would actively lie. */
static void ui_draw_footer(void)
{
    static const char *const FOOT_HOME[] = {"START", "LEN", "MUTE", "PROB"};
    static const char *const FOOT_GRID[] = {"STEP", "LEN", "MUTE", "PROB"};
    /* GLO layer on GRID: K1 START (rev.11), K3 DIV (rev.22); K2 zoom lives on
     * HOME too, so it is named here as well — the modifier is page-independent. */
    static const char *const FOOT_GLO_GRID[] = {"START", "ZOOM", "DIV", "PROB"};
    static const char *const FOOT_GLO_HOME[] = {"START", "ZOOM", "MUTE", "PROB"};
    static const char *const FOOT_EDIT[] = {"PIT", "GAT", "VEL", "PROB", "SEL"};
    static const char *const FOOT_DRUM[] = {"VEL", "GAT", "PROB"};
    static const char *const FOOT_ARP[]  = {"K", "N", "ROT", "LINK"};
    static const uint8_t FX[5] = {8, 64, 120, 176, 204};
    uint8_t k;
    cv_begin(240, 24, TB_BG);
    if (jam_menu) {                              /* the SAVE menu owns the footer */
        for (k = 0; k < 4; k++) {
            if (k == jam_menu_sel)
                cv_rect(FX[k] - 3, 0, 56, 24, TB_TX);       /* highlight: inverted box */
            cv_text(FX[k], 6, &FONT_T, JAM_MENU_LABELS[k],
                    k == jam_menu_sel ? TB_BG : FOOT_GRAY);
        }
        cv_blit(0, 216);
        return;
    }
    if (ui_mode == M_GRID && is_drum_trk() && focus_layer) {
        for (k = 0; k < 3; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_DRUM[k], C_AMB);
    } else if (ui_mode == M_GRID && focus_layer) {
        for (k = 0; k < 5; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_EDIT[k], YELLOW);
    } else if (ui_mode == M_ARP) {
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_ARP[k], FOOT_GRAY);
    } else if (ui_mode == M_SCALE) {
        static const char *const FOOT_SCALE[4] = {"KIND", "KEY", "OFC", ""};
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_SCALE[k], FOOT_GRAY);
    } else if (ui_mode == M_TRKSET) {
        /* K1 row, K2 value, K3 the MUTE/SOLO slider it is everywhere else,
         * K4 the GLOBAL swing (the one number this page edits that is not
         * per-track, so it names itself rather than hiding behind the row) */
        static const char *const FOOT_TRKSET[4] = {"ROW", "VALUE", "MUTE", "SWING"};
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_TRKSET[k], FOOT_GRAY);
    } else if (glo_held && ui_mode != M_ARP) {
        const char *const *f = (ui_mode == M_HOME) ? FOOT_GLO_HOME : FOOT_GLO_GRID;
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, f[k], YELLOW);
    } else {
        const char *const *f = (ui_mode == M_HOME) ? FOOT_HOME : FOOT_GRID;
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, f[k], FOOT_GRAY);
    }
    /* The knob in hand overrides its own column: same slot, same place, but the
     * number instead of the name, in the bar's own text colour so it reads as a
     * live readout and not as another label. Drawn as a box covering the label
     * because a value is not always as wide as the word it replaces ("MUTE"
     * vs "ON", "LEN" vs "4") and a partially overwritten label is worse than
     * either. */
    if (kn_ttl) {
        int x = FX[kn_i];
        const char *v = kn_value_text();
        kn_ttl--;
        cv_rect(x - 3, 0, 52, 24, TB_BG);        /* the column plus its gap */
        cv_text(x, 6, &FONT_T, v, TB_TX);
    }
    cv_blit(0, 216);
}

/* ---- HOME hero: ABSOLUTE TIME axis (rev.19, P = John's pick) ----
 * top bar 20px: TRK nn | PTN 01 | BPM | DIV, right: S nn
 * 16 rows x 12px (y24..216): gutter TRK nn / LEN nn / route U / M.
 *
 * WHY A TIME AXIS: divisions are per-track (01 §1), so a step-index axis
 * silently assumes "all tracks have the same cell length" — a 12-step 1/8
 * loop is twice as long as a 12-step 1/16 loop but drew the same width, so
 * the phase drift on screen was a lie. The internal clock is 24 PPQ and all
 * five divisions divide a quarter exactly (12 / 8 / 6 / 4 / 3 ticks), so
 * there is an exact common grid: x = tick, 0.5px per tick.
 *
 *   4/4 bar = 16 sixteenths = 96 ticks = 48px;  beat = 24 ticks = 12px
 *   span    = 4 bars = 384 ticks = 192px = x47..238
 *   cell px = 6 / 4 / 3 / 2 / 1.5  for 1/8 1/8T 1/16 1/16T 1/32
 *   block w = cell px - 1 gap, min 1px (John: 1/32 is always 1px)
 *
 * 1/16 keeps exactly the rev.18 look (3px pitch, 2px block, 1px gap).
 *
 * BACKGROUND carries both references John asked for:
 *   loop  = ONE 15% hue band anchored at t=0; past its right edge the row is
 *           "outside the loop" and drawn as a dark hatched bed (rev.23 —
 *           the row is NOT tiled with repetitions any more)
 *   meter = bar lines (brighter) and beat lines (dimmer) over the band
 *
 * NOTES: melody rows draw the four voices as separate 1px lines (same slot
 * grammar as the EDIT voice cards, so voice identity is stable) with the
 * line length = that note's gate — a sustained note is literally a long
 * line. Drum rows keep the 9-lane instrument mini-lines.
 *
 * ONE PLAYHEAD PER TRACK (rev.23): drift is a per-track fact — each loop
 * wraps at its own length — so a single shared line could only ever show the
 * global clock. Each row now draws its own head at its own position, and
 * because the meter behind it stays global, drift reads as heads crossing the
 * bar lines at different moments. */
/* TPB lives in ui_glue.c now: ui.c's LED ruler needs it too, and ui.c is
 * included before this file. */
#define HX0      47
#define HXW      192                       /* bar area is always 192px wide */
#define HX1      (HX0 + HXW)               /* 239, independent of zoom */
/* ZOOM (rev.20): the window spans 1/2/4/8 bars but keeps 192px, so zooming
 * changes TICKS PER PIXEL, not the drawing width:
 *   1 bar  96t / 192px = 2.0 px/tick   (1/16 cell = 12px)
 *   2 bar 192t / 192px = 1.0 px/tick   (1/16 cell =  6px)
 *   4 bar 384t / 192px = 0.5 px/tick   (1/16 cell =  3px)  <- default
 *   8 bar 768t / 192px = 0.25px/tick   (1/16 cell = 1.5px)
 * John's original point: 64 steps is the ceiling but most tracks use 16, so
 * at 4 bars a 16-step 1/16 loop only filled a quarter of the screen. */
static uint8_t hero_bars = 4;              /* 1, 2, 4 or 8 */
static int hxspan(void) { return TPB * (int)hero_bars; }
/* tick -> x offset within the bar area (exact integer math, 192/span) */
static int tx(int tick) { return (tick * HXW) / hxspan(); }

/* cell length in 24-PPQ ticks — every division is an exact integer */
static int cell_ticks(uint8_t div)
{
    switch (div) {
    case DS_DIV_8:   return 12;
    case DS_DIV_8T:  return 8;
    case DS_DIV_16:  return 6;
    case DS_DIV_16T: return 4;
    case DS_DIV_32:  return 3;
    default:         return 6;
    }
}
static const char *div_str(uint8_t div)
{
    switch (div) {
    case DS_DIV_8:   return "1/8";
    case DS_DIV_8T:  return "1/8T";
    case DS_DIV_16:  return "1/16";
    case DS_DIV_16T: return "1/16T";
    case DS_DIV_32:  return "1/32";
    default:         return "1/16";
    }
}
/* block width for a cell of ct ticks: cell px minus the 1px gap, min 1.
 * MUST go through tx() — a zoom-independent width would make blocks wider
 * than the cell pitch once zoomed out (at 8 bars a 1/16 cell is 1.5px). */
static int step_w(int ct) { int w = tx(ct) - 1; return w < 1 ? 1 : w; }

/* ---- GRID column ruler: where does a beat/bar fall? (rev.31) ----
 * GRID used to draw no metric structure at all, which left the keybed LEDs as
 * the only thing claiming to say "this is a beat" — and a key page is a bar
 * only at 1/16, so that claim was wrong at four of five divisions. Time now
 * lives on the screen and the keys go back to being pure space (03 §3.2).
 *
 * Must be evaluated PER ROW, not once for the whole grid: at 1/8T and 1/16T a
 * page is 128 and 64 ticks — 1.33 and 0.67 bars — so row r starts at a
 * different position in the bar and the beat phase shifts row to row. A single
 * full-height line would be right on row 0 and wrong below it.
 *
 * Every division divides a quarter exactly (12/8/6/4/3 into 24), so a beat
 * boundary is ALWAYS a cell boundary and the tick can live in the black gutter
 * between cells instead of overprinting content. */
static int grid_tick_kind(uint8_t s, int ct)      /* 0 none / 1 beat / 2 bar */
{
    int t = s * ct;
    if (t % TPB == 0) return 2;
    if (t % (TPB / 4) == 0) return 1;
    return 0;
}
static uint16_t grid_tick_col(int kind)
{
    return kind == 2 ? RGB565(96, 96, 112) : RGB565(58, 58, 70);
}

/* distance (in steps) from s to the next step holding a voice, within the
 * window; 0xFF gate means "hold until that event or the loop end" */
static int next_event_run(uint8_t t, uint8_t s, uint8_t len, uint8_t start)
{
    ds_track *tr = &eng.pat.track[t];
    int rel = (int)((s + 64 - start) & 63), d;
    for (d = 1; d < (int)len - rel; d++) {
        ds_step *nx = &tr->steps[(start + rel + d) & 63];
        if (nx->on && nx->nvoices)
            return d;
    }
    return (int)len - rel;
}
/* Number of pages the 5-dot indicator shows. */
#define HDR_DOTS 5

/* The one header bar every page shares.
 *
 * Why this exists: the bar used to be drawn inline by each painter, and they
 * disagreed -- HOME was 20px with glyphs at y=4, GRID was 16px with glyphs at
 * y=2, ARP/EDIT were 16px at a third offset. Changing page moved the bar and
 * the text, which John called out as the reason the header looked bad before
 * any question of bitmaps came up. One function, one geometry.
 *
 * 24px is what fits: HOME's 16 rows at 12px pitch allow up to 24, ARP's ring
 * starts at y=32, and GRID-drum's nine lanes plus its 100px card force the
 * lane pitch down to 10px so the bar can reach 24 (see draw_grid_drum).
 *
 * Type is FONT_S, 11px caps in a 16px cell -- 38% taller glyphs than FONT_T,
 * which is what makes a single line read at arm's length. The cost of the
 * larger face is horizontal room, paid by deleting the word "TRACK": the pill
 * is already the track colour, so "TRACK 1" was a label restating its own
 * colour. A bare digit in a filled pill says the same thing in 17px instead
 * of 33px.
 */
/* Baseline of the bar's single line. FONT_S puts 11px of caps 2 rows into its
 * 16px cell, so y=5 lands the caps at rows 7..17 of the 24px bar: 6px above and
 * 6px below. Every element in the bar uses it, which is why the pill, the
 * context and the right cluster cannot drift apart vertically. */
#define HDR_TY 5

typedef struct {
    const char *ctx;     /* KCK / 4 BAR -- whatever this page is looking at */
    const char *right;   /* S1, or MUTE */
    uint16_t    right_c; /* colour for `right` */
    uint8_t     dots;    /* 1 = draw the page indicator */
    uint8_t     page;    /* which dot is lit */
    /* A state badge drawn as a FILLED PILL after the dot row, e.g. MUTE / SOLO.
     * The context word alone could not carry it: "MELODIC MUTE" is 13 characters
     * of the same white as "MELODIC", so at a glance the header read as one
     * phrase and the fact that the track is silent did not stand out from the
     * fact that it is melodic (John 2026-10-30). A pill is a shape, so it is
     * legible without reading. */
    const char *badge;
    uint16_t    badge_c;
} hdr_t;

static void ui_draw_header(const hdr_t *h)
{
    /* Pill geometry. The width is FIXED at two digits, not fitted to the
     * number: John's point is that the header must not move when the selection
     * crosses from track 9 to track 10. A fitted pill would grow 7px there and
     * shove every readout to its right, which reads as the whole bar twitching
     * on a knob twist. One digit is centred in the same box, so the bar is
     * identical on all 16 tracks. */
    const int pill_w = 24, pill_h = 18, pill_y = 3;
    int x, i, ndig;
    char *b;

    cv_rect(0, 0, 240, 24, TB_BG);
    cv_rect(0, 23, 240, 1, RGB565(42, 42, 52));      /* floor: the bar owns y0..23 */

    /* track number, knocked out of its own colour */
    b = txt_next();
    fmt_int(b, sel_track + 1);
    ndig = (sel_track >= 9) ? 2 : 1;
    cv_round_rect(6, pill_y, pill_w, pill_h, 6, TRACK_HUE(sel_track));
    cv_text(6 + (pill_w - ndig * 7) / 2, 5, &FONT_S, b, TB_BG);

    /* context, then the right cluster */
    x = 6 + pill_w + 8;
    x = cv_text(x, HDR_TY, &FONT_S, h->ctx, TB_TX);

    if (h->dots) {
        for (i = 0; i < HDR_DOTS; i++) {
            int dx = 211 + i * 6;
            if (i == (int)h->page)
                cv_rect(dx, 10, 3, 3, TB_TX);
            else {
                cv_rect(dx, 10, 3, 3, RGB565(58, 58, 70));
            }
        }
    }
    if (h->right) {
        int rx = (h->dots ? 211 : 240 - 6) - 8 - (int)cv_text_w(&FONT_S, h->right);
        cv_text(rx, HDR_TY, &FONT_S, h->right, h->right_c);
    }
    if (h->badge) {
        /* right-aligned in the same slot as `right`, knocked out of the fill:
         * the badge is a STATE, and a state read as text in the same white as
         * the page's name is not a state, it is a word. */
        int bw = (int)cv_text_w(&FONT_T, h->badge) + 8;
        /* left of the page dots, which own x211..241: a badge that reached under
         * them read as a smudge on the pill (the dots are 3px and dark, so they
         * vanished into the fill) */
        int bx = (h->dots ? 205 : 234) - bw;
        cv_round_rect(bx, 5, bw, 14, 4, h->badge_c);
        cv_text(bx + 4, 7, &FONT_T, h->badge, TB_BG);
    }
}

static void draw_home(void)
{
    uint8_t t;
    hdr_t h;
    cv_begin(240, 224, C_BLACK);
    {   /* Context line: how many BARS the hero shows (rev.25: "顶部直接写1 bar
         * 4bar这样，明确表示目前hero显示出几个bar"), then the pattern's tempo and
         * division. These three were three separate text runs stranded at
         * x=6/168/204 with a 66px hole between them; the hole was the header's
         * real problem, not the absence of a bitmap. */
        char *b = txt_next();
        fmt_int(b, (int32_t)hero_bars);
        strcpy_(txt_end(b), " BAR  ");
        fmt_int(txt_end(b), (int32_t)(eng.bpm + 0.5));
        strcpy_(txt_end(b), "  ");
        strcpy_(txt_end(b), div_str(TRK()->division));
        h.ctx = b;
    }
    /* No loop-start readout here any more (John: "S1表示loop开头似乎没有必要").
     * START is K1's label in the footer and now shows its number while the knob
     * turns, which is where the player is already looking; a second copy in the
     * header was a number with no context that never changed by itself. */
    h.right = (char *)0;
    h.right_c = TB_TX;
    h.dots = 1;
    h.page = 0;
    h.badge = (char *)0;
        ui_draw_header(&h);
    for (t = 0; t < 16; t++) {
        ds_track *tr = &eng.pat.track[t];
        int y = 24 + (int)t * 12;
        uint16_t col = TRACK_HUE(t);
        uint8_t muted = tr->mute;
        uint8_t is_drum = (tr->channel == 9);
        int ct = cell_ticks(tr->division);       /* ticks per step (12/8/6/4/3) */
        int loop_ticks = (int)tr->loop_len * ct; /* window length */
        int bw = step_w(ct);                     /* block width, 1px floor */
        /* pitch range of the whole track (rev.27): the hero row is a pitch
         * axis, so it needs the same pmin/pmax the GRID computes. Scanned once
         * per row per frame — 64 steps x <=4 voices, and only for melodic. */
        uint8_t pmin = 127, pmax = 0;
        if (!is_drum) {
            int r;
            for (r = 0; r < 64; r++) {
                uint8_t vi;
                for (vi = 0; vi < tr->steps[r].nvoices; vi++) {
                    uint8_t n = tr->steps[r].v[vi].note;
                    if (n == 0xFF)
                        continue;
                    if (n < pmin) pmin = n;
                    if (n > pmax) pmax = n;
                }
            }
            if (pmin > pmax) { pmin = 48; pmax = 72; }   /* empty track */
        }
        /* ---- the loop window on the ABSOLUTE step axis (rev.26) ----
         * The window is [loop_start, +loop_len) mod 64 (01 §2), so on screen it
         * starts at loop_start, not at 0. rev.23 drew it at 0 and put a note at
         * loop_start*screen-relative position instead — which made a 12-step
         * track with loop_start=4 show its first 4 slots empty and everything
         * shifted right by 4, disagreeing with the GRID by exactly loop_start
         * cells. John: "都是hero画面有问题，grid画面正常" — the GRID was right:
         * it draws absolute slot indices.
         * Spec 03 §1.1 also says turning K1 moves "亮带整体平移" — the band
         * shifts, the notes stay put. That is only true on absolute axes. */
        int bpx = tx((int)tr->loop_start * ct);  /* window start, px */
        int lpx = tx(loop_ticks);                /* window width, px */
        int wpx = bpx + lpx;                     /* window end, px */
        uint8_t s;
        if (lpx > HXW)
            lpx = HXW;
        if (wpx > HXW)
            wpx = HXW;
        /* gutter (46px): TRK nn | LEN nn | route | M/S */
        cv_text(2, y, &FONT_T, txt_num(0, t + 1), muted ? MUTE_GRAY : RGB565(154, 154, 168));
        cv_text(15, y, &FONT_T, txt_num(0, tr->loop_len), muted ? RGB565(74, 74, 82) : RGB565(106, 106, 120));
        cv_text(28, y, &FONT_T, "U", muted ? RGB565(74, 74, 82) : RGB565(122, 122, 136));
        if (muted)
            cv_text(36, y, &FONT_T, "M", RGB565(224, 160, 64));
        else if (tr->solo)
            cv_text(36, y, &FONT_T, "S", RGB565(224, 160, 64));
        cv_rect(HX0, y, HX1 - HX0, 11, muted ? RGB565(20, 20, 24) : RGB565(29, 29, 38));
        if (muted) {
            if (t == sel_track)
                goto outline;
            continue;
        }
        /* ---- background 1: the loop, drawn ONCE from t=0 (rev.23) ----
         * John 2026-10-19: "不需要铺满hero —— hero 显示 4bar、trk len 8step@1/16
         * 只占 2bar，hero 就应该只显示 2bar，后面暗色表示 loop 外".
         *
         * So the row is no longer tiled with loop repetitions. One loop = one
         * bright band anchored at t=0; everything past it is "outside the
         * loop" and gets a darker, hatched bed. Tiling made every track look
         * equally busy at every zoom, and — the actual reason — it destroyed
         * the reading of DRIFT: a repeated loop has no single restart point to
         * slide against, so phase errors hid inside the repetition. With one
         * band, the band's width IS the loop length and its right edge IS where
         * the loop gives up on the window. */
        cv_rect(HX0 + bpx, y, lpx, 11,
                blend(col, RGB565(29, 29, 38), 200));   /* in-loop: hue, 15% */
        {   int x;
            /* hatching both BEFORE the window and AFTER it: on an absolute
             * axis the out-of-loop area can be on either side (loop_start>0). */
            for (x = HX0; x < HX0 + bpx; x += 4)
                cv_rect(x, y, 1, 11, RGB565(36, 36, 45));
            for (x = HX0 + wpx + ((wpx ? 1 : 0)); x < HX1; x += 4)
                cv_rect(x, y, 1, 11, RGB565(36, 36, 45));
        }
        /* ---- the loop edge: THIS track's ruler (rev.24) ----
         * John: "如果是12step的trk呢？所以不强调对齐bar / 甚至是13step".
         * At 1/16 a 12-step loop is 0.75 bar and a 13-step loop is 0.8125 bar —
         * they never land on a bar line, and a 13-step loop never lands on the
         * same place twice within a pattern. Drawing the bar grid as the
         * brightest thing on the row told the user the opposite: it implied
         * every loop is measured in bars. It is not, that is the whole point of
         * polymeter.
         *
         * So the row gets its own authoritative tick: a 2px full-saturation
         * line in the track's hue at the exact loop length. The bar grid stays
         * as CONTEXT but drops below everything else in brightness — it answers
         * "where am I in the bar", not "where does this loop end". Those are
         * different questions and only the second one is per-track. */
        if (wpx > 0 && wpx < HXW)
            cv_rect(HX0 + wpx - 1, y, 2, 11, col);
        /* ---- background 2: meter (beat 24 ticks, bar 96 ticks)
         * CONTEXT, not ruler (rev.24). John's point about 12- and 13-step
         * loops: a loop that is 0.75 or 0.8125 bars long never lands on a bar
         * line, so the bar grid cannot be the thing the eye measures loops
         * against — it only answers "where am I in the bar".
         * It therefore sits BELOW the loop band in brightness: a bar line is
         * now dimmer than the bed it crosses, so the band and its edge tick
         * stay the loudest thing on the row. Beat spacing follows the zoom, so
         * at 1 bar a beat is 48px and at 8 bars 6px — below 8px the beat lines
         * stop being drawn (they become a comb and the bar lines alone carry
         * the metric). */
        {   int k, bx = tx(24);
            int beats_per_bar = TPB / 24;            /* 4 in 4/4 */
            if (bx >= 8) {
                for (k = 0; k <= hxspan() / 24; k++) {
                    int x = HX0 + tx(k * 24);
                    if (x >= HX1)
                        break;
                    cv_rect(x, y, 1, 11,
                            (k % beats_per_bar) ? RGB565(34, 34, 42) : RGB565(42, 42, 52));
                }
            } else {                                 /* 8-bar: bar lines only */
                for (k = 0; k <= (int)hero_bars; k++) {
                    int x = HX0 + tx(k * TPB);
                    if (x >= HX1)
                        break;
                    cv_rect(x, y, 1, 11, RGB565(42, 42, 52));
                }
            }
        }
        /* ---- background 3 removed: the alternating pass stripes already
         * show where a loop restarts (see background 1). ---- */
        /* ---- notes: absolute slots, inside the window only (rev.26) ----
         * Iterate the WINDOW in play order but draw each note at its ABSOLUTE
         * slot position, so the hero and the GRID agree cell for cell. The
         * iteration order still matters for gate/next_event math (which is
         * play-order semantics); only the x mapping is absolute now. */
        for (s = 0; s < tr->loop_len; s++) {
            uint8_t si = (uint8_t)((tr->loop_start + s) & 63);
            ds_step *st = &tr->steps[si];
            int x = HX0 + tx(((int)si % 64) * ct);
            int rel_end = (int)tr->loop_len - (int)s;   /* steps left in loop */
            if (x >= HX0 + wpx)
                continue;                    /* outside the window: nothing */
            if (st->tie) {
                /* A tie step draws the note it extends, at that note's pitch,
                 * edge to edge across the cell, 1px into the gap before it --
                 * so consecutive ties fuse into one bar and the reading is
                 * "joined means held", same as the GRID. An orphan tie (no
                 * attack before it) has nothing to draw and draws nothing. */
                int prev = prev_attacking_step(tr, si);
                uint8_t pmin2 = 127, pmax2 = 0;
                uint8_t rr, vv;
                if (prev >= 0) {
                    uint8_t pv = tie_pitch_of(tr, (uint8_t)prev);
                    int py;
                    for (rr = 0; rr < 64; rr++)
                        for (vv = 0; vv < tr->steps[rr].nvoices; vv++) {
                            uint8_t nn = tr->steps[rr].v[vv].note;
                            if (nn == 0xFF) continue;
                            if (nn < pmin2) pmin2 = nn;
                            if (nn > pmax2) pmax2 = nn;
                        }
                    if (pmin2 > pmax2) { pmin2 = 48; pmax2 = 72; }
                    if (pmax2 == pmin2) pmax2 = (uint8_t)(pmin2 + 1);
                    py = y + 10 - (((int)pv - (int)pmin2) * 9) / ((int)pmax2 - (int)pmin2);
                    if (py < y) py = y;
                    if (py > y + 10) py = y + 10;
                    cv_rect(x - 1, py, bw + 1, 1, hue12(pv));
                }
                continue;
            }
            if (!st->on || !st->nvoices)
                continue;
            if (is_drum) {
                /* mask must hold NDRUM bits: at 9 lanes a uint8_t loses
                 * bit 8 (the 9th drum) silently — the SHK line simply
                 * never appeared in the hero. */
                uint16_t mask = 0;
                uint8_t li, row = 0, vi;
                cv_rect(x, y, bw, 11, blend(col, RGB565(29, 29, 38), 205));
                for (vi = 0; vi < st->nvoices; vi++) {
                    uint8_t d;
                    for (d = 0; d < NDRUM; d++)
                        if (st->v[vi].note == GM_DRUMS[d]) mask |= (uint16_t)(1u << d);
                }
                for (li = 0; li < NDRUM && row < 4; li++) {
                    if (!(mask & (1u << li)))
                        continue;
                    cv_rect(x, y + 1 + row * 3, bw, 1, D_PAL[li]);
                    row++;
                }
            } else {
            /* melodic: pitch maps VERTICALLY, same normalisation as the GRID
             * (rev.27). John 2026-10-20: "按音高画（像 GRID）：在整条轨的音域内
             * 做 pmin/pmax 映射，音高高低在 hero 上一眼可见".
             *
             * The old code stacked voices by their index in the sorted chord
             * ((sort_n-1-vi)*3), which put every single-note step on the SAME
             * row — demo trk5 spans C5/E5/G5/C6, three octaves, and all four
             * drew at dy=1. Index-into-chord is a fact about how many notes a
             * step holds, not about pitch, so the row carried no melodic
             * information at all.
             *
             * Now the row is a pitch axis: pmin..pmax of the whole track maps
             * to the 11px band, high on top. Same pmin/pmax scan and same
             * rounding as draw_grid_melodic(), so a note sits at the same
             * relative height on both screens. Line length is still the gate,
             * colour is still hue12(note). */
            uint8_t vi;
            voices_sorted(t, si);
            for (vi = 0; vi < sort_n; vi++) {
                int gsteps, gpx, ly, frac;
                uint8_t g = sort_vs[vi][2];
                uint16_t c = hue12(sort_vs[vi][0]);
                if (g == 0xFF)
                    gsteps = next_event_run(t, si, tr->loop_len, tr->loop_start);
                else
                    gsteps = g;
                if (gsteps > rel_end)          /* clamp at loop end */
                    gsteps = rel_end;
                gpx = tx(gsteps * ct);
                if (gpx < 1)
                    gpx = 1;
                if (x + gpx > HX0 + wpx)       /* gate stops at the window edge */
                    gpx = HX0 + wpx - x;
                /* pitch -> row y: 9 usable scanlines inside the 11px band,
                 * same span/rounding as the GRID's cell mapping */
                frac = pmax > pmin
                     ? ((int)sort_vs[vi][0] - (int)pmin) * 256 / ((int)pmax - (int)pmin)
                     : 128;
                if (frac < 0) frac = 0;
                if (frac > 256) frac = 256;
                ly = y + 1 + ((256 - frac) * 9 + 128) / 256;
                if (ly > y + 10) ly = y + 10;
                cv_rect(x, ly, gpx, 1,
                        st->prob < 100 ? blend(c, RGB565(29, 29, 38), 115) : c);
            }
        }
        }
        /* ---- the track's OWN playhead (rev.23) ----
         * John: "这样每个trk都有自己的playhead，可以表现出drift".
         * One global line can only say "this is where the shared clock is";
         * polymorphic drift is a per-track fact — each loop wraps at its own
         * length, so the rows must show where each one actually is. Because
         * every row's time mapping is its own (cells of its own division,
         * band of its own loop length) while the meter behind them is global,
         * drift reads as playheads crossing the bar lines at different times.
         * Position = steps into the loop + the sub-cell fraction, in ticks:
         * the same clock the engine schedules from, so the line cannot lie
         * about the audio. */
        if (eng.playing) {
            /* absolute slot of this track's head (rev.26): pos16 already IS an
             * absolute slot index — the old code subtracted loop_start to get a
             * play-order offset, which only worked while notes were also drawn
             * play-order. Both are absolute now. */
            int ph = HX0 + tx((int)((double)eng.pos16[t] * ct + eng.frac16[t] * ct));
            if (ph >= HX1)
                ph = HX1 - 1;
            cv_rect(ph, y, 1, 11, C_WHITE);
        }
outline:
        if (t == sel_track) {                        /* selected: green outline at 46 / 239 */
            /* The top edge of row 0 would land on y=23, which is the header's
             * floor rule. Under the old 20px bar there were three blank rows
             * between the bar and the first track row, so y-1 was free; with a
             * 24px bar the first row starts right under the rule and the
             * outline has to stay inside the row it belongs to. Clamping keeps
             * the box closed on rows 1..15 and draws the top edge one pixel
             * lower on row 0, where the rule already provides the line. */
            int ty = (y - 1 < 24) ? 24 : y - 1;
            cv_rect(HX0 - 1, ty, (HX1 - HX0) + 2, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 1, y + 11, (HX1 - HX0) + 2, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 1, y, 1, 11, RGB565(127, 217, 74));
            cv_rect(HX1, y, 1, 11, RGB565(127, 217, 74));
        }
    }
    cv_blit_band(0, 0, 216);                         /* the footer painter owns 216+ */
    ui_draw_footer();
}

/* "EDIT" badge at the top-bar right corner — the loud layer flag (r16j has
 * footer color only; John asked for a醒目标记). Yellow box, black text.
 *
 * Centred in the shared 24px bar: the badge is 12px tall, so y=6 leaves 6px
 * above and 5px below. It covers the page dots while the layer is open, which
 * is the intent — a modal layer outranks "which page am I on". */
static void ui_draw_edit_badge(void)
{
    cv_rect(196, 6, 40, 12, YELLOW);
    cv_text(201, 7, &FONT_T, "EDIT", C_BLACK);
}

/* ---- MELODY GRID (r16j drawGrid verbatim) ----
 * 4x22px rows (y24..111), cell bg = loop in/out (#2a2a33 / #16161c),
 * pitch line = hue12(note), rest = gray dashed, bare gate = bottom bar,
 * yellow cursor, white playhead ring, P1-P4 labels, EDIT panel at 112. */
static void draw_grid_melodic(void)
{
    ds_track *tr = TRK();
    uint8_t pmin = 127, pmax = 0;
    int r, c;
    uint8_t start = tr->loop_start, len = tr->loop_len;
    int pos = (int)eng.pos16[sel_track];
    for (r = 0; r < 64; r++) {
        uint8_t vi;
        for (vi = 0; vi < tr->steps[r].nvoices; vi++) {
            int n = tr->steps[r].v[vi].note;
            if (n == 0xFF) continue;
            if (n < pmin) pmin = tr->steps[r].v[vi].note;
            if (n > pmax) pmax = tr->steps[r].v[vi].note;
        }
    }
    if (pmin > pmax) { pmin = 48; pmax = 72; }
    if (pmax == pmin) pmax = (uint8_t)(pmin + 1);

    cv_begin(240, 224, C_BLACK);
    {   /* The bar is shared, but what it reports is not: the melodic grid cares
         * about the step under the cursor and its chord quality, and the DIV
         * readout has to stay here because GLO+K3 changes it on this page.
         * Those three were text runs at x=6/96/150 with no relation between
         * them; now the number is the pill and the rest follows it. */
        hdr_t h;
        char *cn = chord_name(sel_track, cursor);
        char *b = txt_next();
        strcpy_(b, div_str(tr->division));
        strcpy_(txt_end(b), "  ");
        strcpy_(txt_end(b), txt_st(cursor + 1));
        if (*cn) {
            strcpy_(txt_end(b), " ");
            strcpy_(txt_end(b), cn);
        }
        h.ctx = b;
        h.right = (char *)0;                 /* loop start lives on K1's footer slot now */
        h.right_c = TB_TX;
        h.dots = 1;
        h.page = 1;
        h.badge = (char *)0;
        ui_draw_header(&h);
    }
    if (key_shift != 0)
        cv_text(120, 2, &FONT_T, key_shift > 0 ? "+" : "-", YELLOW);
    if (focus_layer)
        ui_draw_edit_badge();
    {
        /* Vertical budget: 240 = 24 header + 96 grid + 100 EDIT panel + 4 gap
         * + 24 footer. The header owns y0..23 (its floor line is at y=23), so
         * the grid MUST start at 24 -- it used to start at 16, which was the
         * 16px-header era, and after the bar grew to 24 the P1 row and its
         * "P1" label were painted straight through it (measured: rows 17..23
         * were cell background, not header).
         *
         * 96px / 4 rows = 24, but a row holds one 1-2px pitch line, so the
         * height is not doing work: 22 is plenty and buys the 8px back without
         * touching the EDIT panel, which is 4x25 dense text and cannot shrink
         * as cheaply. 4x22 = 88, so the grid ends at y=111 exactly as it did
         * before -- only the row height changed, not the space it occupies. */
        const int x0 = 18, cw = 13, chh = 22, gy0 = 24;  /* r16j: x0=18, cw=(240-18-16)/16=13 */
        const int nct = cell_ticks(tr->division);    /* one lookup for the grid */
        struct { int x, y, w; uint16_t c; } ext[16]; /* gate extensions, drawn after */
        int ext_n = 0;                               /* (see the voice loop below) */
        for (r = 0; r < 4; r++)
            for (c = 0; c < 16; c++) {
                uint8_t s = (uint8_t)(r * 16 + c);
                ds_step *st = &tr->steps[s];
                int x = x0 + c * cw, y = gy0 + r * chh;
                uint8_t rel = (uint8_t)((s + 64 - start) & 63);
                uint8_t inWin = rel < len;
                uint8_t vi;
                int tk = grid_tick_kind(s, nct);
                /* cell bg: loop range (bright inside / dark outside) */
                cv_rect(x + 1, y + 1, cw - 2, chh - 2, inWin ? RGB565(42, 42, 51) : RGB565(22, 22, 28));
                /* metric tick in the gutter between cells, so it never covers a
                 * note line (rev.31 — GRID previously had no time structure at
                 * all, which is why the keybed LEDs ended up impersonating it) */
                if (tk)
                    cv_rect(x - 1, y + 1, 1, chh - 1, grid_tick_col(tk));
                if (st->on && st->nvoices == 0)      /* bare gate (r15b/#16) */
                    cv_rect(x + 2, y + chh - 4, cw - 4, 3, blend(D_PAL[(uint8_t)((tr->channel * 3) % 11)], C_BLACK, 153));
                voices_sorted(sel_track, s);         /* pitch-ascending, high on top */
                for (vi = 0; vi < sort_n; vi++) {
                    int span = pmax - pmin;
                    int frac = span ? ((int)sort_vs[vi][0] - pmin) * 256 / span : 128;
                    int lineY = y + 4 + ((256 - frac) * (chh - 11) + 128) / 256 + vi;
                    if (st->tie) {
                        /* A tie does not attack: the note already sounding runs
                         * through this cell. Draw it at the pitch of the step it
                         * extends, in that note's own hue, as a line that fills
                         * the cell edge to edge and reaches 1px PAST the left
                         * edge -- so it visually joins the cell before it. An
                         * attack keeps a 2px margin on both sides, so "joined or
                         * not" is the entire reading, with no extra glyph. */
                        int prev = prev_attacking_step(tr, s);
                        if (prev >= 0) {
                            uint8_t pv = tie_pitch_of(tr, (uint8_t)prev);
                            if (pv != 0xFF) {
                                int pspan = pmax - pmin;
                                int pfrac = pspan ? ((int)pv - pmin) * 256 / pspan : 128;
                                int pY = y + 4 + ((256 - pfrac) * (chh - 11) + 128) / 256;
                                cv_rect(x - 1, pY, cw + 1, 1, hue12(pv));
                            }
                        }
                        continue;                    /* no pitch line of its own */
                    }
                    if (!st->on) {
                        cv_rect(x + 3, lineY, cw - 6, 1, RGB565(102, 102, 114));
                    } else {
                        /* gate = the line's LENGTH (plan D): the attack draws a
                         * 2x2 stub, and the run's extension is COLLECTED here
                         * but painted after the cell loop -- later cells' bg
                         * would otherwise erase the part that crosses them
                         * (measured: the tail survived only inside its own
                         * cell, plus 2px slivers where the next cells' bg
                         * rects didn't reach). Chords: every voice's run
                         * starts here; two voices with the same pitch share a
                         * line (they stack with +vi anyway). */
                        int run = gate_run_cells(tr, s, sort_vs[vi][2]);
                        int end = x + run * cw - 2;
                        int lim = x0 + 16 * cw;      /* the grid's right edge */
                        if (end > lim) end = lim;    /* engine cuts at the window */
                        cv_rect(x + 2, lineY, 2, 2, hue12(sort_vs[vi][0]));
                        if (run > 1 && end > x + 4 && ext_n < 16) {
                            ext[ext_n].x = x + 4;
                            ext[ext_n].y = lineY;
                            ext[ext_n].w = end - (x + 4);
                            ext[ext_n].c = hue12(sort_vs[vi][0]);
                            ext_n++;
                        }
                    }
                }
                if (eng.playing && pos == s)           /* playhead ring */
                    cv_rect(x + 1, y + 1, cw - 2, 1, C_WHITE);
                if (cursor == s) {                   /* cursor: yellow outline */
                    cv_rect(x, y, cw, 1, YELLOW);
                    cv_rect(x, y + chh - 1, cw, 1, YELLOW);
                    cv_rect(x, y, 1, chh, YELLOW);
                    cv_rect(x + cw - 1, y, 1, chh, YELLOW);
                }
                if (c == 0)
                    cv_text(3, y + 4, &FONT_T, txt_join("P", txt_num(0, r + 1)), FOOT_GRAY);
            }
        /* the gate extensions, now that every cell's bg is down: painting them
         * inside the loop let later cells erase the part crossing them */
        {   int i;
            for (i = 0; i < ext_n; i++)
                cv_rect(ext[i].x, ext[i].y, ext[i].w, 1, ext[i].c);
        }
    }
    cv_blit_band(0, 0, 112);                         /* edit panel + footer own 112+ */
    ui_draw_edit_panel();                            /* rev.12 embedded panel */
    ui_draw_footer();
}

/* ---- DRUM GRID (03 §3): NDRUM lanes x 11px (7px cell + 4px gap), cells 9x9
 * GAP4, hollow/solid, gate dimming, playhead column, readout strip 56px.
 * Vertical budget (rev.13b): 16 top + 9x11 lanes (99) + 4x25 panel (100) +
 * 24 footer = 239px. Same main-area height as the melodic 4x24 grid. */
static void draw_grid_drum(void)
{
    uint8_t page = cursor >> 4;
    int li, c, x0 = 30, y0 = 24, lh = 10, gap = 3;
    int cw = (240 - x0) / 16;                        /* 13 */
    uint8_t lane_in_use[NDRUM];
    int pos = (int)eng.pos16[sel_track];
    for (li = 0; li < NDRUM; li++) {                 /* r16i: which drums are voiced? */
        int s;
        lane_in_use[li] = 0;
        for (s = 0; s < 64 && !lane_in_use[li]; s++) {
            uint8_t nv = step_nv(sel_track, s), vi;
            for (vi = 0; vi < nv; vi++)
                if (step_note(sel_track, s, vi) == GM_DRUMS[li]) lane_in_use[li] = 1;
        }
    }
    cv_begin(240, 160, C_BLACK);
    {   /* Drum page: the context is the drum under the cursor and the lane grid
         * page (1/4 .. 4/4), which is the only place that number is shown.
         * The bar is 24px here too, which is why the lane pitch below drops to
         * lh=10 / gap=3: nine lanes must still end above the 100px card at
         * y=115. cellh = lh - gap stays 7px, so the cells are the same size as
         * before and only 1px of row spacing pays for the taller bar.
         *
         * `ST n` is the cursor's step number and it is not decoration: the grid
         * is 16 cells wide and the cursor is a 1px yellow outline, so without
         * the number there is no way to tell step 7 from step 8 by reading. The
         * old 20px bar carried it and the 24px rewrite dropped it -- this puts
         * it back in the same order the old bar had (page, then step). */
        hdr_t h;
        char *b = txt_next();
        strcpy_(b, div_str(TRK()->division));
        strcpy_(txt_end(b), "  P");
        fmt_int(txt_end(b), page + 1);
        strcpy_(txt_end(b), "/4");
        strcpy_(txt_end(b), "  ");
        strcpy_(txt_end(b), txt_st(cursor + 1));
        h.ctx = b;
        h.right = GM_SHORT[drum_sel];            /* which drum the cursor is on */
        h.right_c = TRACK_HUE(sel_track);
        h.dots = 1;
        h.page = 1;
        h.badge = (char *)0;
        ui_draw_header(&h);
    }
    if (focus_layer)
        ui_draw_edit_badge();
    /* GRID metric ruler (rev.31), drawn once for the whole lane block: in the
     * drum grid the vertical axis is INSTRUMENT, so every lane in column c is
     * the same step — one full-height tick per column is both correct and
     * cheaper than 144 little ones. (The melodic grid cannot do this: its
     * vertical axis is pitch and its rows are different steps, so there the
     * tick is per cell.) */
    {   int c;
        const int nct = cell_ticks(TRK()->division);
        for (c = 0; c < 16; c++) {
            int tk = grid_tick_kind((uint8_t)(page * 16 + c), nct);
            if (tk)
                cv_rect(x0 + c * cw + gap / 2 - 1, y0, 1, NDRUM * lh - 1,
                        grid_tick_col(tk));
        }
    }
    for (li = 0; li < NDRUM; li++) {
        int y = y0 + li * lh;
        uint8_t sel = (li == drum_sel);
        cv_text(2, y + 2, &FONT_T, GM_SHORT[li], sel ? RGB565(201, 201, 212) : RGB565(106, 106, 118));
        for (c = 0; c < 16; c++) {
            uint8_t s = (uint8_t)(page * 16 + c);
            ds_step *st = &TRK()->steps[s];
            int x = x0 + c * cw + gap / 2, cy = y + gap / 2;
            int cellw = cw - gap, cellh = lh - gap;
            uint8_t on = 0, vi, hit_gate = 4;
            for (vi = 0; vi < st->nvoices; vi++)
                if (st->v[vi].note == GM_DRUMS[li]) { on = 1; hit_gate = st->v[vi].gate; }
            if (on) {                                /* solid hit (35% when not sel lane) */
                uint16_t col = blend(D_PAL[li], C_BLACK, sel ? 0 : 166);
                int gf = (hit_gate == 0xFF) ? cellw : cellw * ((hit_gate * 64) / 32) / 256;
                cv_rect(x, cy, cellw, cellh, col);
                if (s == cursor && sel && gf < cellw) /* gate: dim the un-gated span */
                    cv_rect(x + gf, cy, cellw - gf, cellh, blend(col, C_BLACK, 179));
                if (s == cursor && sel) {            /* cursor hit: white outline */
                    cv_rect(x - 1, cy - 1, cellw + 2, 1, BRIGHTTX);
                    cv_rect(x - 1, cy + cellh, cellw + 2, 1, BRIGHTTX);
                    cv_rect(x - 1, cy - 1, 1, cellh + 2, BRIGHTTX);
                    cv_rect(x + cellw, cy - 1, 1, cellh + 2, BRIGHTTX);
                }
            } else {                                 /* hollow square */
                uint16_t stroke = sel ? RGB565(86, 86, 100) : RGB565(46, 46, 58);
                cv_rect(x, cy, cellw, 1, stroke);
                cv_rect(x, cy + cellh - 1, cellw, 1, stroke);
                cv_rect(x, cy, 1, cellh, stroke);
                cv_rect(x + cellw - 1, cy, 1, cellh, stroke);
                if (st->on && sel && lane_in_use[li])/* bare-gate tick (r16i) */
                    cv_rect(x + 1, cy + cellh - 3, cellw - 2, 2, blend(D_PAL[li], C_BLACK, 115));
            }
        }
    }
    /* the playhead: 1px white, nothing else.
     *
     * There used to be a 13px "veil" column under it (blend(WHITE, BLACK, 219),
     * i.e. 14% white). Two things were wrong with it. It was painted AFTER the
     * cells, so it darkened the hits it was pointing at -- the current step's
     * drum measured RGB(32,32,32) while its neighbour stayed legible -- and even
     * moved beneath them it left a 1px+2px grey stripe in the cell gaps, which
     * reads as a second, fainter bar next to the line. John: "整列都不画 veil,
     * 只留 1px 白线". The melodic grid never had a veil; now neither does this
     * one, and the line says the same thing in both. */
    if (eng.playing && pos >= page * 16 && pos < page * 16 + 16) {
        int px = x0 + (pos - page * 16) * cw;
        cv_rect(px, y0, 1, NDRUM * lh, C_WHITE);
    }
    cv_blit_band(0, 0, 115);                         /* the card + footer own 115+ */
    ui_draw_drum_card();                             /* 100px single-drum card */
    ui_draw_footer();
}

/* ---- the live value behind the knob in hand ------------------------------
 *
 * This lives at the bottom of the file on purpose: it reads hero_bars and
 * div_str and the per-mode step fields, all of which are declared later than
 * the footer that calls it. Moving it up top to sit next to kn_poke() looked
 * tidier and did not compile.
 *
 * Each branch mirrors the page's own encoder handler, because the number shown
 * must be the number that just changed. EDIT melodic has PIT/GAT/VEL/PROB (note
 * is PIT and moves in octaves, so it is shown as a MIDI note rather than a
 * step count); EDIT drum has VEL/GAT/PROB on the voice under the cursor; GRID
 * bare is step/prob; ARP is K/N/ROT/LINK.
 */
static const char *kn_value_text(void)
{
    ds_track *tr = TRK();
    ds_step *st = &tr->steps[cursor];
    char *b = txt_next();
    if (ui_mode == M_ARP) {
        /* matches FOOT_ARP = {K, N, ROT, LINK} and ui_arp_encoder */
        if (kn_i == 0)      fmt_int(b, tr->eu_k);
        else if (kn_i == 1) fmt_int(b, tr->eu_n ? tr->eu_n : 16);
        else if (kn_i == 2) fmt_int(b, tr->eu_rot);
        else                strcpy_(b, tr->eu_on ? "LINK" : "FREE");
        return b;
    }
    if (ui_mode == M_GRID && focus_layer) {
        if (is_drum_trk()) {
            int hit = -1, vi;
            for (vi = 0; vi < st->nvoices; vi++)
                if (st->v[vi].note == GM_DRUMS[drum_sel]) hit = vi;
            if (kn_i == 0)      fmt_int(b, hit < 0 ? 0 : st->v[hit].vel);
            else if (kn_i == 1) fmt_int(b, hit < 0 ? 0 : st->v[hit].gate);
            else                fmt_int(b, st->prob);
        } else {
            int vo = edit_voice;
            if (kn_i == 0)      fmt_int(b, (vo < st->nvoices) ? st->v[vo].note : 0);
            else if (kn_i == 1) fmt_int(b, (vo < st->nvoices) ? st->v[vo].gate : 0);
            else if (kn_i == 2) fmt_int(b, (vo < st->nvoices) ? st->v[vo].vel : 0);
            else                fmt_int(b, st->prob);
        }
        return b;
    }
    /* HOME and bare GRID. The labels differ per page and so does the meaning:
     *
     *   HOME      START  LEN   MUTE  PROB
     *   GRID      STEP   LEN   MUTE  PROB
     *   GRID+GLO  START  ZOOM  DIV   PROB
     *
     * (FOOT_HOME / FOOT_GRID / FOOT_GLO_* in ui_draw_footer, and the matching
     * branches in ui_home_encoder / ui_grid_encoder.) This function has to
     * follow those, not its own idea of the knob.
     *
     * Two display conventions matter here:
     *  - positions are 1-based. loop_start is 0..63 and cursor is 0..63, but
     *    the player counts steps from 1 (the grid draws ST1 first), so both
     *    print +1. John: "k1应该从1开始".
     *  - K2 is the track's loop_len on both bare pages, NOT hero_bars. The row
     *    gutter's LEN column is its other readout and it moved correctly while
     *    this value did not, which is exactly what John saw. hero_bars is the
     *    zoom ladder and only appears under GLO.
     */
    if (glo_held) {
        if (kn_i == 0)       fmt_int(b, (int32_t)tr->loop_start + 1);   /* START */
        else if (kn_i == 1)  fmt_int(b, (int32_t)hero_bars);            /* ZOOM  */
        else if (kn_i == 2)  strcpy_(b, div_str(tr->division));         /* DIV   */
        else                 fmt_int(b, st->prob);                      /* PROB  */
    } else if (ui_mode == M_GRID) {
        if (kn_i == 0)       fmt_int(b, (int32_t)cursor + 1);           /* STEP  */
        else if (kn_i == 1)  fmt_int(b, tr->loop_len);                  /* LEN   */
        else if (kn_i == 2)  strcpy_(b, k3_state_name(tr, sel_track));  /* MUTE/SOLO */
        else                 fmt_int(b, st->prob);                      /* PROB  */
    } else {
        if (kn_i == 0)       fmt_int(b, (int32_t)tr->loop_start + 1);   /* START */
        else if (kn_i == 1)  fmt_int(b, tr->loop_len);                  /* LEN   */
        else if (kn_i == 2)  strcpy_(b, k3_state_name(tr, sel_track));  /* MUTE/SOLO */
        else                 fmt_int(b, st->prob);                      /* PROB  */
    }
    return b;
}
