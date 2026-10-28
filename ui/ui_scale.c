/* ui_scale.c — SCALE page + one-finger chords (John 2026-10-28).
 * SEL enters/exits M_SCALE; the page edits three live-only settings:
 *   K1 scale kind   K2 tonic   K3 OFC on/off
 * Everything here feeds the KEYBED at input time, never the playback: notes
 * are snapped when played/written (the hero always tells the truth about the
 * data), chords are plain voices (the 4-voice step model already holds them).
 * Live-only like K3 solo: pack.c untouched; boot = MAJ / tonic C / OFC off.
 *
 * Keybed zones (27 keys F3..G5):
 *   left  0..6   (F3..E4)   OCT- (key 0) / OCT+ (key 6) transposes the ROOT
 *                           zone by octaves; 1..5 spare (fall through)
 *   mid   7..18  (F4..B4)   ROOT: the 12 chromatic roots, printed C..B
 *   right 19..26 (C5..G5)   COMPONENTS: MAJ MIN DIM 6 7 M7 SUS4 ADD9
 * A chord = root + up to 3 component keys; nonsense sets play nothing.
 * While a root is held, changing the set re-voices live (jam follows); the
 * GRID write happens once, when the set resolves to a chord. */

enum { SC_MAJ, SC_MIN, SC_DOR, SC_MIX, SC_HMI, SC_PMAJ, SC_PMIN, SC_BLU, SC_CHR, SC_NKIND };
static const char *const SC_NAME[SC_NKIND] =
    {"MAJ", "MIN", "DOR", "MIX", "HMI", "P.MAJ", "P.MIN", "BLU", "CHR"};
/* pitch-class sets, bit n = tonic+n sounds */
static const uint16_t SC_SET[SC_NKIND] = {
    0x0AB5, 0x09AD, 0x06AD, 0x0AD5, 0x09B5, 0x05A9, 0x04A9, 0x0CEF, 0x0FFF
};

enum { OC_MAJ, OC_MIN, OC_DIM, OC_SIX, OC_SEV, OC_M7, OC_SUS, OC_ADD9, OC_NCOMP };
static const char *const OC_NAME[OC_NCOMP] = {"MAJ", "MIN", "DIM", "6", "7", "M7", "SUS4", "ADD9"};

typedef struct { uint8_t n; int8_t t[4]; } Chord;   /* semitones above the root, sorted */

/* resolve the held component set. Model: root + a third slot (MIN/DIM/SUS4,
 * else the major third) + fifth + optional seventh + optional ninth, sorted,
 * then fit 4 voices: 5 tones drop the FIFTH (the jazz convention, one drop
 * covers every overflow: 9th, 6/9, 7sus9...). Nonsense sets play nothing. */
static Chord oc_resolve(uint8_t mask)
{
    Chord out = {0, {0, 0, 0, 0}};
    uint8_t thirds = mask & ((1u << OC_MIN) | (1u << OC_DIM) | (1u << OC_SUS));
    uint8_t sevens = mask & ((1u << OC_SEV) | (1u << OC_M7));
    uint8_t has6   = (mask >> OC_SIX) & 1u;
    uint8_t has9   = (mask >> OC_ADD9) & 1u;
    int8_t t[5], tmp;
    uint8_t n = 0, i, j;
    if ((thirds & (thirds - 1)) || (sevens & (sevens - 1)) || (has6 && sevens))
        return out;                                   /* two of a slot / 6 with a 7th */
    t[n++] = 0;                                       /* the root */
    if (thirds & (1u << OC_MIN))      t[n++] = 3;
    else if (thirds & (1u << OC_DIM)) t[n++] = 3;
    else if (thirds & (1u << OC_SUS)) t[n++] = 5;     /* the 4th replaces the 3rd */
    else                              t[n++] = 4;     /* implied MAJ */
    if (thirds & (1u << OC_DIM)) t[n++] = 6;          /* the dim's flat fifth */
    else if (!has6)              t[n++] = 7;          /* the fifth (the 6th replaces it) */
    else                         t[n++] = 9;
    if (sevens & (1u << OC_SEV)) t[n++] = 10;
    if (sevens & (1u << OC_M7))  t[n++] = 11;
    if (has9)                    t[n++] = 14;         /* closed position: the 9 an octave up is 14 */
    /* insertion sort (n <= 5) */
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && t[j] < t[j - 1]; j--)
            { tmp = t[j]; t[j] = t[j - 1]; t[j - 1] = tmp; }
    if (n == 5) {                                     /* drop the fifth: t[2] sits between third and 7th/9th */
        for (i = 2; i < 4; i++) t[i] = t[i + 1];
        n = 4;
    }
    out.n = n;
    for (i = 0; i < n; i++) out.t[i] = t[i];
    return out;
}

/* ---- state ---- */
static uint8_t sc_kind = SC_MAJ;                  /* K1 */
static uint8_t sc_tonic = 0;                      /* K2, pitch class 0..11 (0=C) */
static uint8_t sc_ofc;                            /* K3: one-finger chords on */
static int8_t sc_root_oct;                        /* the ROOT zone's octave shift (keys 0/6) */
static uint8_t sc_root_key = 0xFF;                /* held root's KEY ID, 0xFF none */
static uint8_t sc_mask;                           /* held component keys' bitmask */
static uint8_t sc_last_valid;                     /* the last set that resolved (LED aid) */

/* snap a MIDI note into the scale: nearest scale tone, tie takes the lower */
static int ui_snap(int note)
{
    int pc, deg, best = -1, bestd = 99, d;
    if (sc_kind == SC_CHR) return note;               /* chromatic: no snapping */
    pc = ((note - sc_tonic) % 12 + 12) % 12;
    for (deg = 0; deg < 12; deg++) {
        if (!(SC_SET[sc_kind] >> deg & 1)) continue;
        d = pc - deg; if (d < 0) d = -d;
        if (d < bestd) { bestd = d; best = deg; }     /* strict < : a tie takes the lower */
    }
    return note - (pc - best);
}

/* the sounding tones right now (root alone, or the resolved chord), snapped */
static uint8_t sc_tones_n;
static uint8_t sc_tones[4];
static uint8_t sc_prev_n;
static uint8_t sc_prev[4];

/* apply a revoice as a diff: notes that left get off (+jam up), notes that
 * arrived get on (+jam down). Without it a sus4 swap leaves the old third
 * hanging (a held E under an F never stops). */
static void sc_apply(void)
{
    uint8_t i, j, gone;
    for (i = 0; i < sc_prev_n; i++) {
        gone = 1;
        for (j = 0; j < sc_tones_n; j++)
            if (sc_tones[j] == sc_prev[i]) gone = 0;
        if (gone) {
            ds_live_off(&eng, sel_track, sc_prev[i]);
            ds_jam_capture(&eng, sc_prev[i], midi_vel, 0);
        }
    }
    for (i = 0; i < sc_tones_n; i++) {
        gone = 1;
        for (j = 0; j < sc_prev_n; j++)
            if (sc_prev[j] == sc_tones[i]) gone = 0;
        if (gone) {
            ds_live_on(&eng, sel_track, sc_tones[i], midi_vel);
            ds_jam_capture(&eng, sc_tones[i], midi_vel, 1);
        }
    }
    sc_prev_n = sc_tones_n;
    for (i = 0; i < sc_tones_n; i++) sc_prev[i] = sc_tones[i];
}

static void sc_revoice(uint8_t root_note)            /* rebuild sc_tones from state */
{
    Chord c = oc_resolve(sc_mask);
    uint8_t i;
    if (sc_mask && c.n == 0) { sc_tones_n = 0; return; }   /* nonsense set: silence */
    sc_tones_n = 0;
    if (!sc_mask) {                                  /* root alone: one snapped note */
        sc_tones[sc_tones_n++] = (uint8_t)ui_snap(root_note);
        return;
    }
    for (i = 0; i < c.n && sc_tones_n < 4; i++)
        sc_tones[sc_tones_n++] = (uint8_t)ui_snap(root_note + c.t[i]);
}

/* ---- input ----
 * The hook sits at the TOP of panel_key_handler (before every existing path).
 * Returns 1 = consumed. Drums bypass (ch9 tracks never snap/chord), and the
 * EDIT layer keeps raw notes (it edits written voices). */
static uint8_t scale_key_hook(uint8_t key, uint8_t down)
{
    uint8_t is_drum = is_drum_trk();
    if (is_drum) return 0;                        /* drums: everything raw */
    if (!sc_ofc) {                                /* OFC off: snap only */
        int note = 53 + (int)key + key_shift;
        note = ui_snap(note);
        if (down) {
            ds_live_on(&eng, sel_track, (uint8_t)note, midi_vel);
            ds_jam_capture(&eng, (uint8_t)note, midi_vel, 1);
        } else
            ds_live_off(&eng, sel_track, (uint8_t)note);
        return 1;
    }
    /* OFC on: the three zones */
    if (key == 0 || key == 6) {                   /* OCT- / OCT+ : edge action */
        if (down) {
            sc_root_oct += (key == 0) ? -12 : 12;
            if (sc_root_oct < -24) sc_root_oct = -24;
            if (sc_root_oct > 24)  sc_root_oct = 24;
        }
        return 1;                                 /* the shift keys never sound */
    }
    if (key >= 7 && key <= 18) {                  /* a root */
        int root = 60 + (key - 7) + sc_root_oct;  /* printed C4..B4 + the zone shift */
        if (down) {
            if (sc_root_key == 0xFF) {            /* first root takes over */
                sc_root_key = key;
                sc_revoice((uint8_t)root);
                sc_apply();
            }
            /* a second root while one is held is ignored: one left hand */
        } else if (sc_root_key == key) {
            sc_root_key = 0xFF;
            sc_mask = 0;
            sc_tones_n = 0;
            sc_apply();
        }
        return 1;
    }
    if (key >= 19) {                              /* a component */
        uint8_t bit = (uint8_t)(1u << (key - 19));
        if (down && sc_root_key != 0xFF && !(sc_mask & bit)) {
            uint8_t i;
            uint8_t prev_n;
            sc_mask |= bit;
            sc_revoice(60 + (sc_root_key - 7) + sc_root_oct);
            if (sc_tones_n == 0) {                /* the set went nonsense: silence */
                return 1;
            }
            sc_last_valid = sc_mask;
            sc_apply();
            if (ui_mode == M_GRID) {              /* the GRID write: once, when the set resolves */
                ds_track *tr = TRK();
                ds_step *st = &tr->steps[cursor];
                uint8_t t;
                while (st->nvoices)               /* replace the step, as stepEntry does */
                    ds_voice_remove(tr, cursor, st->v[0].note);
                for (t = 0; t < sc_tones_n; t++)
                    ds_voice_add(tr, cursor, sc_tones[t], midi_vel, 1);
                st->on = 1;
                edit_voice = 0;
                if (cursor < 63u) cursor++;
            }
        } else if (!down && (sc_mask & bit)) {    /* component up: live follows, no write */
            sc_mask &= (uint8_t)~bit;
            if (sc_root_key != 0xFF) {
                sc_revoice(60 + (sc_root_key - 7) + sc_root_oct);
                sc_apply();
            }
        }
        return 1;
    }
    return 0;                                     /* keys 1..5 spare: fall through */
}

/* the panel's key lights for this layer: the right hand must SEE its zone */
static void scale_leds(uint32_t *litK, uint32_t *litB, uint8_t is_drum)
{
    uint8_t i;
    if (sc_ofc && !is_drum) {
        for (i = 0; i < OC_NCOMP; i++)               /* the 8 component keys */
            *litK |= 1u << (19 + i);
        if (sc_root_key != 0xFF)                     /* the held root */
            *litK |= 1u << sc_root_key;
    }
    if (ui_mode == M_SCALE)
        *litB |= 1u << B_SEL;                        /* the page is open: SEL is live */
}

/* ---- the SCALE page (painter) ---- */
static void draw_scale(void)
{
    uint8_t i;
    /* title + the three values (each names itself: no mode words) */
    cv_text(8, 8, &FONT_T, "SCALE", C_AMB);
    cv_text(8, 28, &FONT_T, SC_NAME[sc_kind], TB_TX);
    cv_text(96, 28, &FONT_T, ui_note_name(sc_tonic), TB_TX);
    cv_text(152, 28, &FONT_T, sc_ofc ? "OFC ON" : "OFC OFF", sc_ofc ? C_HI : C_GRAY);

    /* the 12-cell interval strip: one cell per semitone from the tonic */
    for (i = 0; i < 12; i++) {
        int in = (SC_SET[sc_kind] >> i & 1);
        int x = 24 + i * 16;
        uint16_t col = in ? TB_TX : C_LINE;
        if (in)
            cv_rect(x, 56, 14, 18, col);
        if (i == 0) {                                /* the tonic: an outline (4 rules) */
            cv_rect(x - 2, 54, 18, 1, C_AMB); cv_rect(x - 2, 75, 18, 1, C_AMB);
            cv_rect(x - 2, 54, 1, 22, C_AMB); cv_rect(x + 15, 54, 1, 22, C_AMB);
        }
    }

    /* the chord components, lit when in the last valid set (or all dim) */
    cv_text(8, 96, &FONT_T, "CHORD", C_GRAY);
    for (i = 0; i < OC_NCOMP; i++) {
        int x = 8 + (i % 4) * 58, y = 116 + (i / 4) * 22;
        uint16_t col = (sc_last_valid >> i & 1) ? C_HI : C_GRAY;
        cv_text(x, y, &FONT_T, OC_NAME[i], col);
    }

    /* the keybed zones: ROOT C..B (12), COMPONENTS (8) */
    cv_text(8, 168, &FONT_T, "ROOT", C_GRAY);
    for (i = 0; i < 12; i++) {
        int x = 52 + i * 14;
        cv_text(x, 168, &FONT_S, ui_note_name(i), (sc_root_key != 0xFF && (uint8_t)(sc_root_key - 7) == i) ? C_AMB : C_GRAY);
    }
    cv_text(8, 186, &FONT_T, "OCT", C_GRAY);
    {
        char *b = txt_next();
        strcpy_(b, "");
        fmt_int(txt_end(b), sc_root_oct / 12);
        cv_text(52, 186, &FONT_S, b, C_GRAY);
    }
    cv_blit_band(0, 0, 216);                          /* the footer painter owns 216+ */
}

/* ---- the page's encoders (K1..K4) + SEL ---- */
static void scale_encoder(uint8_t enc, int8_t det)
{
    if (enc < EN_K1 || enc > EN_K4) return;
    if (enc == EN_K1) {
        int v = (int)sc_kind + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v >= SC_NKIND) v = SC_NKIND - 1;          /* dead stops, no wrap */
        sc_kind = (uint8_t)v;
    } else if (enc == EN_K2) {
        int v = (int)sc_tonic + (det > 0 ? 1 : -1);
        if (v < 0) v = 0;
        if (v > 11) v = 11;
        sc_tonic = (uint8_t)v;
    } else if (enc == EN_K3) {
        if (det) sc_ofc = !sc_ofc;                    /* any detent flips */
        if (!sc_ofc) { sc_mask = 0; sc_last_valid = 0; }
    }                                                  /* K4: spare */
    kn_poke(enc);
}

static uint8_t scale_button(uint8_t btn)               /* returns 1 = consumed */
{
    if (btn == B_SEL) {                                /* SEL toggles the page */
        ui_mode = (ui_mode == M_SCALE) ? M_HOME : M_SCALE;
        return 1;
    }
    if (ui_mode != M_SCALE) return 0;
    if (btn == B_HOME || btn == B_SEQ || btn == B_ARP)
        ui_mode = M_HOME;                              /* any screen switch leaves */
    return 0;                                          /* PLAY/REC/SAVE work as ever */
}
