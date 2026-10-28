/* ui_scale.c — SCALE page + one-finger chords (John 2026-10-28).
 * SEL enters/exits M_SCALE; the page edits three live-only settings:
 *   K1 scale kind   K2 tonic   K3 OFC on/off
 * Everything here feeds the KEYBED at input time, never the playback: notes
 * are snapped when played/written (the hero always tells the truth about the
 * data), chords are plain voices (the 4-voice step model already holds them).
 * Live-only like K3 solo: pack.c untouched; boot = MAJ / tonic C / OFC off.
 *
 * Keybed zones (27 keys F3..G5):
 *   left  0..6   (F3..E4)   DEGREES I..VII: while a chord is held, the chord's
 *                           root walks the scale and the quality recomputes
 *                           from it (I=maj ii=min ...); key up returns to the
 *                           held root. Live-only: the pattern keeps the anchor.
 *   mid   7..18  (F4..B4)   ROOT: the 12 chromatic roots, printed C..B (the
 *                           panel's OCT buttons transpose the whole keybed as
 *                           everywhere else)
 *   right 19..26 (C5..G5)   COMPONENTS: MAJ MIN DIM 6 7 M7 SUS4 ADD9 — flavors
 *                           over the scale's own quality (MAJ/MIN force the
 *                           third, DIM flats the fifth, SUS4 takes the fourth,
 *                           7/M7 add a seventh, 6 swaps the fifth, ADD9 adds
 *                           the ninth). Flavor notes are deliberate and skip
 *                           the snap; scale-born tones stay scale-true.
 * A chord = root + up to 3 component keys; nonsense sets play nothing.
 * While a root is held, changing the set re-voices live (jam follows); the
 * GRID write happens once, when the set resolves to a chord. */

enum { SC_MAJ, SC_MIN, SC_DOR, SC_MIX, SC_HMI, SC_PMAJ, SC_PMIN, SC_BLU, SC_CHR, SC_NKIND };
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
static uint8_t sc_root_key = 0xFF;                /* held root's KEY ID, 0xFF none */
static uint8_t sc_deg_key = 0xFF;                 /* held degree key (0..6), 0xFF none */
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

/* the ROOT zone's note for key k (the panel's OCT buttons shift everything) */
static int sc_root_note(uint8_t k)
{
    int note = 60 + (int)k - 7 + key_shift;          /* printed C4..B4 */
    if (note < 12) note += 12;
    if (note > 108) note -= 12;
    return note;
}

/* the pitch class of scale degree d (0..6: the d-th scale tone above the tonic),
 * and the stacked-third chord the scale implies there: the third is 2 scale
 * steps up, the fifth 4. Returns tone count; t[] = semitones above the degree
 * root. Degrees need the SC_SET bit per step, walking the mask circularly. */
static uint8_t sc_degree_chord(uint8_t d, int8_t *t)
{
    uint8_t n = 0, i;
    int8_t offs[7];
    for (i = 0; i < 12 && n < 7; i++)                /* the scale's own tones, low to high */
        if (SC_SET[sc_kind] >> i & 1)
            offs[n++] = (int8_t)i;
    /* semitone offset of scale-step s above the degree root */
    #define SC_STEP(s) (offs[((d + (s)) % 7 + 7) % 7] - offs[d] + ((((d + (s)) % 7) < d) ? 12 : 0))
    t[0] = 0;
    t[1] = SC_STEP(2);                               /* the third */
    t[2] = SC_STEP(4);                               /* the fifth */
    t[3] = SC_STEP(6);                               /* the seventh */
    return 4;
    #undef SC_STEP
}

/* apply the right hand's flavor keys onto a scale-stacked chord (in place) */
static void sc_flavor(int8_t *t, uint8_t *pn)
{
    uint8_t m = sc_mask, n = *pn;
    if (m & (1u << OC_SUS)) t[1] = 5;                /* the fourth replaces the third */
    else if (m & (1u << OC_MIN)) t[1] = 3;           /* force the minor third */
    else if (m & (1u << OC_MAJ)) t[1] = 4;           /* force the major third */
    if (m & (1u << OC_DIM)) { t[1] = 3; t[2] = 6; }  /* the dim: minor third + flat fifth */
    if (m & (1u << OC_SIX)) t[2] = 9;                /* the sixth replaces the fifth */
    if ((m & (1u << OC_SEV)) || (m & (1u << OC_M7)))
        t[3] = (m & (1u << OC_M7)) ? 11 : 10;        /* the seventh (replaces the scale's) */
    if (m & (1u << OC_ADD9) && n < 4) { t[n++] = 14; }   /* the ninth, closed position */
    *pn = n;
}

static void sc_revoice(uint8_t root_note)            /* rebuild sc_tones from state */
{
    Chord c = oc_resolve(sc_mask);
    uint8_t i;
    if (sc_deg_key != 0xFF) {                        /* a degree walk is live: the scale speaks */
        int8_t t[4];
        uint8_t n = sc_degree_chord(sc_deg_key, t), d;
        int deg_pc, root_pc, delta;
        /* the degree root, nearest to the held root's register */
        d = sc_deg_key;
        deg_pc = -1;
        {   uint8_t k = 0;
            for (i = 0; i < 12; i++)
                if (SC_SET[sc_kind] >> i & 1) {
                    if (k == d) { deg_pc = (int)i; break; }
                    k++;
                }
        }
        if (deg_pc < 0) return;                      /* no scale: silence */
        root_pc = ((root_note % 12) + 12) % 12;
        delta = ((deg_pc - root_pc) + 18) % 12 - 6;  /* the shortest way round (+-6) */
        root_note = (uint8_t)(root_note + delta);
        sc_flavor(t, &n);
        sc_tones_n = 0;
        for (i = 0; i < (int)n && sc_tones_n < 4; i++)
            sc_tones[sc_tones_n++] = (uint8_t)(root_note + t[i]);
        return;
    }
    if (sc_mask && c.n == 0) { sc_tones_n = 0; return; }   /* nonsense set: silence */
    sc_tones_n = 0;
    if (!sc_mask) {                                  /* root alone: one snapped note */
        sc_tones[sc_tones_n++] = (uint8_t)ui_snap(root_note);
        return;
    }
    for (i = 0; i < c.n && sc_tones_n < 4; i++)      /* flavor tones: no snap (deliberate) */
        sc_tones[sc_tones_n++] = (uint8_t)(root_note + c.t[i]);
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
    if (key <= 6) {                               /* DEGREES I..VII: the chord walks */
        if (down && sc_root_key != 0xFF && sc_deg_key != key) {
            sc_deg_key = key;                     /* the scale re-voices, live */
            sc_revoice(sc_root_note(sc_root_key));
            sc_apply();
        } else if (!down && sc_deg_key == key) {
            sc_deg_key = 0xFF;                    /* back to the held root's chord */
            sc_revoice(sc_root_note(sc_root_key));
            sc_apply();
        }
        return 1;                                 /* degree keys never sound alone */
    }
    if (key >= 7 && key <= 18) {                  /* a root */
        if (down) {
            if (sc_root_key == 0xFF) {            /* first root takes over */
                sc_root_key = key;
                sc_revoice(sc_root_note(key));
                sc_apply();
            }
            /* a second root while one is held is ignored: one left hand */
        } else if (sc_root_key == key) {
            sc_root_key = 0xFF;
            sc_deg_key = 0xFF;
            sc_mask = 0;
            sc_tones_n = 0;
            sc_apply();
        }
        return 1;
    }
    if (key >= 19) {                              /* a component */
        uint8_t bit = (uint8_t)(1u << (key - 19));
        if (down && sc_root_key != 0xFF && !(sc_mask & bit)) {
            sc_mask |= bit;
            sc_revoice(sc_root_note(sc_root_key));
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
                sc_revoice(sc_root_note(sc_root_key));
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

/* ---- the SCALE page (painter) ----
 * Three settings, three rows, full names (John: 三行写全称); below them the
 * interval strip and a REAL piano-keybed diagram (proper black/white offsets,
 * zone brackets under the keys, the held root lit). */
static const char *const SC_FULL[SC_NKIND] = {
    "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "HARM.MINOR", "PENT.MAJOR",
    "PENT.MINOR", "BLUES", "CHROMATIC"
};

static void draw_scale(void)
{
    uint8_t i;
    /* the three settings, one per row, full names */
    cv_text(8, 10, &FONT_T, "SCALE", C_AMB);
    cv_text(96, 10, &FONT_T, SC_FULL[sc_kind], TB_TX);
    cv_text(8, 28, &FONT_T, "TONIC", C_AMB);
    cv_text(96, 28, &FONT_T, ui_note_name(sc_tonic), TB_TX);
    cv_text(8, 46, &FONT_T, "CHORD KEYS", C_AMB);
    cv_text(96, 46, &FONT_T, sc_ofc ? "ON" : "OFF", sc_ofc ? C_HI : C_GRAY);

    /* the keybed IS the scale display (John: 不在 scale 中的键 grey out): 27
     * keys F3..G5 in real piano arrangement. In-scale keys render true piano
     * (white bright, black dark); out-of-scale keys grey out. The tonic's
     * pitch class gets an amber outline everywhere it appears. */
    {
        const uint8_t WHITE_KEYS[16] = PLAT_WHITE_KEYS;   /* from plat.h */
        const uint8_t BLACK_LOW[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};  /* key -> lower white ordinal */
        const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
        int x0 = 14, y0 = 72, kw = 11, kp = 13;
        for (i = 0; i < 16; i++) {                   /* the white keys */
            uint8_t k = WHITE_KEYS[i];
            int in_scale = (SC_SET[sc_kind] >> (k % 12) & 1);
            int tonic = ((k % 12) == sc_tonic);
            uint16_t col = in_scale ? C_WHITE : C_GRAY;
            if (sc_root_key != 0xFF && k == sc_root_key) col = C_AMB;
            cv_rect(x0 + i * kp, y0, kw, 40, col);
            if (tonic) {                             /* the tonic: an outline (4 rules) */
                cv_rect(x0 + i * kp - 1, y0 - 1, kw + 2, 1, C_AMB);
                cv_rect(x0 + i * kp - 1, y0 + 40, kw + 2, 1, C_AMB);
                cv_rect(x0 + i * kp - 1, y0 - 1, 1, 42, C_AMB);
                cv_rect(x0 + i * kp + kw, y0 - 1, 1, 42, C_AMB);
            }
        }
        for (i = 0; i < 11; i++) {                   /* the black keys, real offsets */
            uint8_t k = BLACK_KEY[i];
            int in_scale = (SC_SET[sc_kind] >> (k % 12) & 1);
            int x = x0 + BLACK_LOW[i] * kp + kw - 3; /* straddling the boundary */
            uint16_t col = in_scale ? C_BLACK : C_LINE;   /* out of scale: dim grey */
            if (sc_root_key != 0xFF && k == sc_root_key) col = C_AMB;
            cv_rect(x, y0, 7, 24, col);
        }
        /* zone brackets: a 3px rule under each zone + its label. The zones are
         * key-id ranges but the geometry is white-ordinal space (the blacks
         * hang between whites): DEG = whites 0..3, ROOT = 4..10, CHORD = 11..15 */
        cv_rect(x0, y0 + 43, 4 * kp - 2, 3, C_AMB);
        cv_text(x0, y0 + 50, &FONT_T, "DEG", C_AMB);
        cv_rect(x0 + 4 * kp, y0 + 43, 7 * kp - 2, 3, C_GRAY);
        cv_text(x0 + 4 * kp + (7 * kp - 4 * 7) / 2, y0 + 50, &FONT_T, "ROOT", C_GRAY);
        cv_rect(x0 + 11 * kp, y0 + 43, 5 * kp - 2, 3, C_HI);
        cv_text(x0 + 11 * kp, y0 + 50, &FONT_T, "CHORD", C_HI);
        /* the component legend (the CHORD zone's keys), lit when in the set */
        for (i = 0; i < OC_NCOMP; i++) {
            int x = 8 + (i % 4) * 58, y = y0 + 70 + (i / 4) * 15;
            uint16_t col = (sc_last_valid >> i & 1) ? C_HI : C_GRAY;
            cv_text(x, y, &FONT_T, OC_NAME[i], col);
        }
    }
    cv_blit_band(0, 0, 216);                          /* the footer painter owns 216+ */
    ui_draw_footer();                                 /* the SCALE footer (else stale pixels stay) */
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
