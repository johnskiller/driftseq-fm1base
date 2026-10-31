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

/* CHROMATIC first: it is the default and the least surprising thing for K1 to
 * be sitting on (John 2026-10-31: scale 应该把chromatic作为默认，放第一个). The
 * order is the knob's travel, so "first" is the dead stop at the bottom. */
enum { SC_CHR, SC_MAJ, SC_MIN, SC_DOR, SC_MIX, SC_HMI, SC_PMAJ, SC_PMIN, SC_BLU, SC_NKIND };
/* pitch-class sets, bit n = tonic+n sounds */
static const uint16_t SC_SET[SC_NKIND] = {
    0x0FFF,                                    /* CHR  everything */
    0x0AB5,                                    /* MAJ  0 2 4 5 7 9 11 */
    0x05AD,                                    /* MIN  0 2 3 5 7 8 10 */
    0x06AD,                                    /* DOR  0 2 3 5 7 9 10 */
    0x06B5,                                    /* MIX  0 2 4 5 7 9 10 */
    0x09AD,                                    /* HMI  0 2 3 5 7 8 11 */
    0x0295,                                    /* PMAJ 0 2 4 7 9 */
    0x04A9,                                    /* PMIN 0 3 5 7 10 */
    0x04E9                                     /* BLU  0 3 5 6 7 10 */
};

enum { OC_MAJ, OC_SEV, OC_M7, OC_MIN, OC_DIM, OC_ADD9, OC_SUS, OC_SIX, OC_NCOMP };
static const char *const OC_NAME[OC_NCOMP] = {"MAJ", "7", "M7", "MIN", "DIM", "ADD9", "SUS4", "6"};

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
    if ((thirds & (thirds - 1)) || (sevens & (sevens - 1)))
        return out;                                   /* two of a slot */
    /* 6 + a seventh is legal now: the fifth gives way at n==5 below
     * (6+7 = the 13th chord's voicing, 6+M7 = maj13 — John: 支持) */
    t[n++] = 0;                                       /* the root */
    int aug = (thirds & (1u << OC_DIM)) && (mask & 1u);   /* MAJ+DIM = augmented */
    if (thirds & (1u << OC_MIN))      t[n++] = 3;
    else if (aug)                     t[n++] = 4;     /* the major third returns */
    else if (thirds & (1u << OC_DIM)) t[n++] = 3;
    else if (thirds & (1u << OC_SUS)) t[n++] = 5;     /* the 4th replaces the 3rd */
    else                              t[n++] = 4;     /* implied MAJ */
    if (thirds & (1u << OC_DIM))
        t[n++] = aug ? 8 : 6;                         /* aug's #5 vs dim's b5 */
    else
        t[n++] = 7;                                   /* the fifth stays */
    if (has6)                    t[n++] = 9;          /* the sixth on top (DIM+6: 0,3,6,9 = the dim7) */
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
static uint8_t sc_kind = SC_CHR;                  /* K1 (John: chromatic by default) */
static uint8_t sc_tonic = 0;                      /* K2, pitch class 0..11 (0=C) */
static uint8_t sc_ofc;                            /* K3: 0 = OFF, 1 = GATE, 2 = LATCH */
static uint8_t sc_root_key = 0xFF;               /* Root SELECTION: persists after release */
static uint8_t sc_gate = 0xFF;                   /* the physically held root (GATE): 0xFF = silent */                /* held root's KEY ID, 0xFF none */
static uint8_t sc_deg_key = 0xFF;                 /* held degree key (0..6), 0xFF none */
static uint8_t sc_mask;                           /* held component keys' bitmask */

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

/* ---- the OFC-off gesture (John 2026-10-31) --------------------------------
 * OFC off used to consume every key and write nothing, so step record on a
 * melody track only ever worked with OFC ON - the shipped default is OFF.
 * OFC off is now a plain keyboard that writes one step per GESTURE: notes go
 * down, and the step is written when the LAST one comes up.
 *
 * Why on release rather than on press: a chord has to reach one step, and on
 * press the second note of a chord would already be a step late. Writing on
 * the last release is what makes C-E-G land as one multi-voice step without
 * OFC engaged. What the player hears is live audition either way, so the
 * timing of the WRITE is invisible in performance; only the grid differs.
 *
 * Two things this must remember:
 *   cap_n / cap[]  the pitches this gesture will write, in press order
 *   rel_n / rel[]  the pitches still physically down; the write fires at zero
 * (the name readout's sc_held[] above is the same set, but it is the READOUT's
 *  state - it is what the page prints, and it must stay one thing only.) */
static uint8_t cap_n;
static uint8_t cap[DS_VOICES];
static uint8_t rel_n;
static uint8_t rel[27];

/* Only the two grid pages write steps. The chord layer still SOUNDS everywhere
 * (that is what it is for), so the page check belongs on the write and not on
 * the key: TRACK SET auditions the PROGRAM you are choosing and says so in its
 * own comment, GLOBAL auditions and taps tempo, and the hero is the hero. */
static uint8_t sc_write_ok(void)
{
    return ui_mode == M_GRID || ui_mode == M_SCALE;
}

/* Write this gesture's pitches to the cursor step, then advance - the same
 * contract as the GRID's stepEntry (override the step, revive the gate, cursor
 * +1 dead stop), so the layer does not invent a second recording rule. Nothing
 * held at all (or nothing in the first DS_VOICES) writes nothing: a stray tap
 * that snapped onto an already-down pitch must not eat a step. */
static void sc_off_write(void)
{
    ds_track *tr = TRK();
    ds_step *st = &tr->steps[cursor];
    uint8_t i;
    if (!cap_n || !sc_write_ok()) return;
    while (st->nvoices)
        ds_voice_remove(tr, cursor, st->v[0].note);
    for (i = 0; i < cap_n; i++)
        ds_voice_add(tr, cursor, cap[i], midi_vel, 1);
    st->on = 1;
    edit_voice = 0;
    if (cursor < 63u) cursor++;
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
static void sc_restrike(void)                       /* a new voicing under a
                                                    * held root re-articulates
                                                    * the whole chord: the ear
                                                    * wants the full strike,
                                                    * not one entering note */
{
    uint8_t i;
    for (i = 0; i < sc_prev_n; i++)
        ds_live_off(&eng, sel_track, sc_prev[i]);
    sc_prev_n = 0;
    sc_apply();
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
    /* the seventh is OPT-IN (John 2026-10-29: the natural degrees are the
     * triads — i=Cmaj, ii=Dmin, iii=Emin, iv=Fmaj; the 7/M7 keys add it) */
    if (sc_mask & ((1u << OC_SEV) | (1u << OC_M7))) {
        t[3] = SC_STEP(6);
        return 4;
    }
    return 3;
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
    if (m & (1u << OC_M7))
        t[3] = 11;                                   /* M7 forces the major seventh;
                                                      * plain 7 keeps the scale's own
                                                      * (diatonic) seventh from sc_degree_chord */
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
        int deg_pc;
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
        /* the degree's pitch hangs off the TONIC, never off the held root
         * (John 2026-10-29: the root zone is for out-of-key chords; the OCT
         * keys own the register, so a root must not transpose the walk) */
        root_note = (uint8_t)(60 + (sc_tonic + deg_pc) % 12 + key_shift);
        sc_flavor(t, &n);
        sc_tones_n = 0;
        for (i = 0; i < (int)n && sc_tones_n < 4; i++)
            sc_tones[sc_tones_n++] = (uint8_t)(root_note + t[i]);
        return;
    }
    if (sc_mask && c.n == 0) { sc_tones_n = 0; return; }   /* nonsense set: silence */
    sc_tones_n = 0;
    if (!sc_mask) {                                  /* root alone: its own pitch.
                                                      * NO snap (John 2026-10-29): OFC's
                                                      * root zone is the out-of-key
                                                      * path, and with OFC off the whole
                                                      * keybed is already the snap */
        sc_tones[sc_tones_n++] = (uint8_t)root_note;
        return;
    }
    for (i = 0; i < c.n && sc_tones_n < 4; i++)      /* flavor tones: no snap (deliberate) */
        sc_tones[sc_tones_n++] = (uint8_t)(root_note + c.t[i]);
}


/* the GRID write: replace the step under the cursor with the Active Chord
 * (root-down writes the single snapped note when no flavor is selected) */
static void sc_grid_write(void)
{
    ds_track *tr;
    if (!sc_write_ok()) return;
    tr = TRK();
    ds_step *st = &tr->steps[cursor];
    uint8_t t;
    while (st->nvoices)                           /* replace the step, as stepEntry does */
        ds_voice_remove(tr, cursor, st->v[0].note);
    for (t = 0; t < sc_tones_n; t++)
        ds_voice_add(tr, cursor, sc_tones[t], midi_vel, 1);
    st->on = 1;
    edit_voice = 0;
    if (cursor < 63u) cursor++;
}

/* ---- input ----
 * The hook sits at the TOP of panel_key_handler (before every existing path).
 * Returns 1 = consumed. Drums bypass (ch9 tracks never snap/chord), and the
 * EDIT layer keeps raw notes (it edits written voices). */
static void chord_name_of(char *buf, const uint8_t *tones, uint8_t n);
static const char *tone_name_of(uint8_t i, const uint8_t *tones, uint8_t n);

uint8_t sc_held_n;                               /* OFC off: how many keys are down now */
static uint8_t sc_held[27];                      /* OFC off: the notes they sound, ascending
                                                  * (John 2026-10-30: 关闭和弦的时候，弹奏也
                                                  * 希望显示音名，能组成和弦也显示出来).
                                                  * Sorted, so the chord name reads from the
                                                  * lowest note as the root -- the way the
                                                  * GRID page names a step's voices. */

/* OFC off is a plain keyboard, so the held keys ARE the answer: name them, and
 * name the chord when they are one. Returns 0 when nothing is down. */
uint8_t sc_held_names(char *chord, char *notes)
{
    uint8_t i, j, t[27], n = sc_held_n;
    char *q = notes;
    *chord = 0;
    *notes = 0;
    if (!n) return 0;
    for (i = 0; i < n; i++) t[i] = sc_held[i];
    for (i = 1; i < n; i++) {                    /* insertion sort, low to high */
        uint8_t v = t[i];
        for (j = i; j > 0 && t[j - 1] > v; j--) t[j] = t[j - 1];
        t[j] = v;
    }
    chord_name_of(chord, t, n);
    for (i = 0; i < n; i++) {
        const char *nm = tone_name_of(i, t, n);  /* interval-aware spelling: Cm is C Eb G */
        while (*nm) *q++ = *nm++;
        if (i + 1 < n) *q++ = ' ';
    }
    *q = 0;
    return 1;
}

static uint8_t scale_key_hook(uint8_t key, uint8_t down)
{
    uint8_t is_drum = is_drum_trk();
    if (is_drum) return 0;                        /* drums: everything raw */
    /* The EDIT layer edits written voices, so the chord layer must not eat its
     * keys. This gate was missing on the OFC-off branch, so EDIT was dead
     * whenever CHORD KEYS was off. */
    if (focus_layer) return 0;
    /* NOTHING ELSE IS GATED HERE. The chord layer is how the instrument sounds,
     * so it sounds on every page: gating the hook on ui_mode made CHORD KEYS
     * silently stop working on the hero ("回到hero后自动退出了ofc", John
     * 2026-10-31). What must be gated is the WRITE, and that is sc_write_ok():
     * the two grid pages write, every other page only auditions (TRACK SET
     * auditions the program you are choosing, GLOBAL auditions, the hero is the
     * hero). */
    if (!sc_ofc) {                                /* OFC off: a plain keyboard that
                                                   * records one step per gesture */
        int note = 53 + (int)key + key_shift;
        uint8_t i;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        note = ui_snap(note);
        if (down) {
            ds_live_on(&eng, sel_track, (uint8_t)note, midi_vel);
            ds_jam_capture(&eng, (uint8_t)note, midi_vel, 1);
            /* two sets, two jobs: sc_held is what the page NAMES (pitches
             * sounding), rel/cap is what the step will CONTAIN (pitches
             * pressed). A key whose snap lands on a pitch already down adds to
             * neither - it is the same pitch, not a second press. */
            for (i = 0; i < rel_n; i++)
                if (rel[i] == (uint8_t)note) break;
            if (i == rel_n) {
                if (rel_n < 27) rel[rel_n++] = (uint8_t)note;
                if (cap_n < DS_VOICES) cap[cap_n++] = (uint8_t)note;
                if (sc_held_n < 27) {
                    for (i = 0; i < sc_held_n; i++)
                        if (sc_held[i] == (uint8_t)note) break;
                    if (i == sc_held_n) sc_held[sc_held_n++] = (uint8_t)note;
                }
            }
        } else {
            ds_live_off(&eng, sel_track, (uint8_t)note);
            ds_jam_capture(&eng, (uint8_t)note, midi_vel, 0);
            for (i = 0; i < sc_held_n; i++)
                if (sc_held[i] == (uint8_t)note) {
                    sc_held[i] = sc_held[--sc_held_n];
                    break;
                }
            for (i = 0; i < rel_n; i++)
                if (rel[i] == (uint8_t)note) {
                    rel[i] = rel[--rel_n];
                    break;
                }
            /* the last key up ends the gesture: this is where the step is
             * written. Not on press - see sc_off_write(). */
            if (!rel_n) {
                sc_off_write();
                cap_n = 0;
            }
        }
        return 1;
    }
    /* OFC on: the three zones. Selection vs sounding (John's review 2026-10-29):
     * sc_root_key / sc_mask = persistent SELECTIONS (a release keeps them);
     * sounding is GATE — the notes live while the root is physically held
     * (sc_gate). A LATCH sounding mode is specced but awaits the stop
     * gesture decision. Component keys TOGGLE: press a lit one to unselect. */
    if (key <= 6) {                               /* DEGREES I..VII: the chord walks */
        if (sc_kind == SC_CHR)
            return 1;                             /* John: CHR has no scale degrees —
                                                   * the object doesn't exist; chord
                                                   * input = the root keys only */
        /* the degree key IS its own gate (John's 1b): its pitch hangs off the
         * tonic, so holding a root first would be an empty gesture. sc_gate
         * carries the degree key id (0..6) while the walk sounds. */
        if (down) {
            sc_deg_key = key;
            sc_gate = key;
            sc_revoice(0);                        /* the root arg is moot on this path */
            if (sc_tones_n == 0) return 1;
            sc_restrike();
            sc_grid_write();
        } else if (sc_deg_key == key) {
            if (sc_ofc == 2) return 1;            /* LATCH: the latched chord keeps
                                                   * sounding (selection persists) */
            sc_deg_key = 0xFF;
            sc_gate = 0xFF;
            sc_tones_n = 0;                       /* GATE: the walk ends: silence */
            sc_apply();
        }
        return 1;
    }
    if (key >= 7 && key <= 18) {                  /* a root */
        if (down) {
            sc_root_key = key;                    /* the new root takes over the
                                                   * selection AND the gate — the
                                                   * old chord's notes go off in
                                                   * the restrike */
            sc_gate = key;
            sc_deg_key = 0xFF;
            sc_revoice(sc_root_note(key));
            if (sc_tones_n == 0) return 1;        /* the set went nonsense */
            sc_restrike();
            sc_grid_write();
        } else if (sc_gate == key) {
            sc_deg_key = 0xFF;
            if (sc_ofc == 2) return 1;            /* LATCH: the release keeps the
                                                   * chord sounding (review §五.2);
                                                   * the gate stays latched */
            sc_gate = 0xFF;                       /* GATE: root up = sound off; the
                                                   * selection stays */
            sc_tones_n = 0;
            sc_apply();
        }
        return 1;
    }
    if (key >= 19) {                              /* a component: TOGGLE */
        uint8_t bit = (uint8_t)(1u << (key - 19));
        if (down) {
            uint8_t activating = !(sc_mask & bit);    /* John's rule: SELECTING a
                                                       * component strikes the chord,
                                                       * UNSELECTING only edits the
                                                       * selection (no new notes) */
            sc_mask ^= bit;                       /* press lit = unselect (the
                                                   * way back to the basic chord) */
            if (sc_gate == 0xFF)
                return 1;                         /* selection only: no root held —
                                                   * the next root-press picks it up */
            sc_revoice(sc_root_note(sc_gate));
            if (sc_tones_n == 0) {                /* the set went nonsense: silence */
                sc_tones_n = 0;
                sc_apply();
                return 1;
            }
            if (activating) {                     /* the new chord speaks */
                sc_restrike();
                sc_grid_write();
            } else {
                sc_apply();                       /* a diff: the dropped tone goes
                                                   * off, nothing re-articulates,
                                                   * the step is untouched */
            }
        }
        return 1;
    }
    return 0;                                     /* keys 1..5 spare: fall through */
}


/* a chord tone's NAME with the spelling its function calls for: a flat third,
 * fifth and seventh read Eb / Gb / Bb (NOTE_NAMES is sharp-only, which turns
 * a C minor into "C D# G"). i = 0 is the root.
 *
 * Takes the set rather than reading sc_tones[]: the Active Chord is not the only
 * thing on this page that needs spelling now (OFC off names what is being played,
 * see sc_held_names). */
static const char *tone_name_of(uint8_t i, const uint8_t *tones, uint8_t n)
{
    static const char *const FLAT[12] = {"C", "Db", "D", "Eb", "E", "F",
                                         "Gb", "G", "Ab", "A", "Bb", "B"};
    static const char *const SHARP[12] = {"C", "C#", "D", "D#", "E", "F",
                                          "F#", "G", "G#", "A", "A#", "B"};
    uint8_t iv;
    if (!i) return ui_note_name((uint8_t)(tones[0] % 12));
    iv = (uint8_t)((tones[i] - tones[0]) % 12);
    if (iv == 3 || iv == 6 || iv == 10 || iv == 1)   /* b3, b5, b7, b9 */
        return FLAT[(tones[i]) % 12];
    if (iv == 8)                                     /* #5 */
        return SHARP[(tones[i]) % 12];
    return FLAT[(tones[i]) % 12];                 /* naturals spell the same */
}

static const char *sc_tone_name(uint8_t i) { return tone_name_of(i, sc_tones, sc_tones_n); }

/* the ACTIVE chord's NAME, read back from the interval set it will send
 * (John 2026-10-29: "除了音名，也要写出和弦名", written the way players write
 * them: Cm, CM7, C7sus4 — not CMAJ7). Rules, not a lookup table, so every set
 * the panel can build gets an honest name; a set with no common name prints
 * "C?" rather than something invented. */
static void chord_name_of(char *buf, const uint8_t *tones, uint8_t n)
{
    uint16_t iv = 0;
    const char *nm;
    char *q = buf;
    uint8_t i, maj3, min3, sus4, p2, p5, b5, s5, m7, M7, s6, ninth;
    if (!n) { *buf = 0; return; }
    nm = ui_note_name((uint8_t)(tones[0] % 12));
    while (*nm) *q++ = *nm++;
    for (i = 1; i < n; i++)                          /* intervals above the root */
        iv |= (uint16_t)(1u << ((tones[i] - tones[0]) % 12));
    if (!iv) { *q = 0; return; }                     /* a single note: its name only */
    maj3  = (iv >> 4) & 1u;
    min3  = (iv >> 3) & 1u;
    sus4  = (iv >> 5) & 1u;
    p2    = (iv >> 2) & 1u;
    p5    = (iv >> 7) & 1u;
    b5    = (iv >> 6) & 1u;
    s5    = (iv >> 8) & 1u;
    s6    = (iv >> 9) & 1u;
    m7    = (iv >> 10) & 1u;
    M7    = (iv >> 11) & 1u;
    ninth = p2 && (maj3 || min3);                    /* a third plus a 2nd = add9 */
    #define PUT(z_) do { const char *w_ = (z_); while (*w_) *q++ = *w_++; } while (0)
    /* the special shapes first: they do not follow the tertian template */
    if (min3 && b5 && s6 && !m7 && !M7)   { PUT("dim7"); goto done; }
    if (min3 && b5 && M7 && !s6)          { PUT("dim(maj7)"); goto done; }
    if (min3 && b5 && !s6 && !m7 && !M7)  { PUT("dim"); goto done; }
    if (maj3 && s5 && !M7 && !s6 && !ninth) { PUT("aug"); goto done; }
    /* sus chords name the EXTENSION first — C7sus4, CM7sus4, C9sus4 (as
     * players write them), never Csus47 */
    if (sus4) {
        if (ninth)      PUT("9");
        else if (s6)    PUT("13");
        else if (m7)    PUT("7");
        else if (M7)    PUT("M7");
        PUT("sus4");
        if (b5)         PUT("b5");
        if (!ninth && !s6 && m7 == 0 && M7 == 0) { /* a plain sus triad: nothing more */ }
        goto done;
    }
    if (min3)      PUT("m");
    if (ninth && (m7 || M7))        PUT(m7 ? "9" : "M9");
    else if (s6 && (m7 || M7))      PUT(m7 ? "13" : "M13");
    else {
        if (m7)      PUT("7");
        else if (M7) PUT("M7");
        if (s6)      PUT("6");
        if (ninth)   PUT("add9");
    }
    if (b5)          PUT("b5");
    else if (s5)     PUT("(#5)");
    if (!maj3 && !min3 && p5 && !b5 && !m7 && !M7 && !s6) PUT("5");  /* root + fifth */
done:
    *q = 0;
    #undef PUT
}

void sc_chord_name(char *buf, uint8_t n) { chord_name_of(buf, sc_tones, n); }

/* the keybed's chord markers as a bitmask (the painter and the host probe) */
uint32_t sc_marks(void)
{
    uint32_t m = 0;
    uint8_t i;
    for (i = 0; i < sc_tones_n; i++) {
        int k = (int)sc_tones[i] - 53 - key_shift;
        if (k >= 0 && k <= 26) m |= 1u << k;
    }
    return m;
}

/* the name of the current Active Chord, for the painter and the host probe */
void sc_chord_name_str(char *buf) { sc_chord_name(buf, sc_tones_n); }

/* The panel's key lights for the SCALE page. John, 2026-10-30: "在scale状态下
 * 不需要0,4,8,12的键盘标尺（标尺只对drum有用）反而是左边级数7个键，右边和弦
 * 8个键需要dim，然后和弦按下有效的需要亮起".
 *
 * So on this page the bed is the CHORD KEYBOARD and nothing else: the seven
 * degree keys and the eight component keys sit DIM as the zones they are, and
 * what is selected lights. The step ruler and the step lights are the GRID's
 * (ui_leds skips them here) -- the ruler divides 16 steps, and a step has no
 * meaning on a page that writes one chord per press and walks the cursor.
 *
 * "有效" (valid) is the point of the lit set: a component that makes the chord
 * nonsense -- two thirds (MIN+DIM, MIN+SUS, ...) or two sevenths (7+M7) -- is a
 * key press that produces nothing (oc_resolve returns n == 0 and sc_revoice
 * silences), so lighting it would claim a chord that is not there. The root is
 * different: it always sounds its own pitch, so it always lights. */
static void scale_leds(uint32_t *litK, uint32_t *dimK, uint32_t *litB, uint8_t is_drum)
{
    uint8_t i;
    if (ui_mode == M_SCALE && !is_drum) {
        /* the degree zone: keys 0..6 sit dim -- except CHR, which has no
         * degrees at all (the object does not exist, John 2026-10-29) */
        for (i = 0; i <= 6; i++)
            if (sc_kind != SC_CHR) *dimK |= 1u << i;
        if (sc_deg_key != 0xFF)                      /* the walking degree lights */
            *litK |= 1u << sc_deg_key;
        if (sc_ofc) {
            uint8_t valid = oc_resolve(sc_mask).n != 0;
            for (i = 0; i < OC_NCOMP; i++)           /* the component zone: dim base */
                *dimK |= 1u << (19 + i);
            /* a selected component lights, but only while the set still makes a
             * chord; the moment it does not, the whole group stops claiming it
             * (the screen says the same thing: sc_chord_name is empty) */
            if (valid)
                for (i = 0; i < OC_NCOMP; i++)
                    if (sc_mask >> i & 1u) *litK |= 1u << (19 + i);
            if (sc_root_key != 0xFF)                 /* the selected root lights */
                *litK |= 1u << sc_root_key;
        } else {
            /* OFC off: the bed is the plain scale keyboard -- every key is live
             * (it plays a snapped note), so the whole bed sits dim and the held
             * root, if any, lights. Components do not exist in this mode. */
            for (i = 7; i <= 18; i++)
                *dimK |= 1u << i;
            if (sc_root_key != 0xFF && sc_root_key >= 7)
                *litK |= 1u << sc_root_key;
        }
        *litB |= 1u << B_SEL;                        /* the page is open: SEL is live */
        return;
    }
    /* not the SCALE page: nothing here (ui_leds owns the bed there) */
    (void)litK; (void)dimK; (void)litB;
}

/* ---- the SCALE page (painter) ----
 * Three settings, three rows, full names (John: 三行写全称); below them the
 * interval strip and a REAL piano-keybed diagram (proper black/white offsets,
 * zone brackets under the keys, the held root lit). */
static const char *const SC_FULL[SC_NKIND] = {
    "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "HARM.MINOR",
    "PENT.MAJOR", "PENT.MINOR", "BLUES"
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
    if (sc_ofc)                                     /* John: K4 = GATE / LATCH */
        cv_text(96, 46, &FONT_T, sc_ofc == 2 ? "ON LATCH" : "ON GATE", C_HI);
    else
        cv_text(96, 46, &FONT_T, "OFF", C_GRAY);



    /* the keybed IS the scale display (John: 不在 scale 中的键 grey out): 27
     * keys F3..G5 in real piano arrangement. In-scale keys render true piano
     * (white bright, black dark); out-of-scale keys grey out. The tonic's
     * pitch class gets an amber outline everywhere it appears. */
    {
        const uint8_t WHITE_KEYS[16] = PLAT_WHITE_KEYS;   /* from plat.h */
        const uint8_t BLACK_LOW[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};  /* key -> lower white ordinal */
        const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
        int x0 = 14, y0 = 72, kw = 11, kp = 13;
        uint32_t marked = sc_marks();                /* the Active Chord's own keys
                                                      * (John: 音名同时在上方键盘反映) */
        for (i = 0; i < 16; i++) {                   /* the white keys */
            uint8_t k = WHITE_KEYS[i];
            int pc = (k + 5) % 12;                   /* id 0 = F3: pc = (F3 + k) % 12 */
            int in_scale = (SC_SET[sc_kind] >> ((pc + 12 - sc_tonic) % 12) & 1);
            int tonic = (pc == sc_tonic);
            uint16_t col = in_scale ? C_WHITE : C_GRAY;
            if (sc_root_key != 0xFF && k == sc_root_key) col = C_AMB;
            cv_rect(x0 + i * kp, y0, kw, 40, col);
            if (marked >> k & 1u)                    /* a chord tone: its foot goes amber */
                cv_rect(x0 + i * kp + 1, y0 + 34, kw - 2, 5, C_HI);
            if (tonic) {                             /* the tonic: an outline (4 rules) */
                cv_rect(x0 + i * kp - 1, y0 - 1, kw + 2, 1, C_AMB);
                cv_rect(x0 + i * kp - 1, y0 + 40, kw + 2, 1, C_AMB);
                cv_rect(x0 + i * kp - 1, y0 - 1, 1, 42, C_AMB);
                cv_rect(x0 + i * kp + kw, y0 - 1, 1, 42, C_AMB);
            }
        }
        for (i = 0; i < 11; i++) {                   /* the black keys, real offsets */
            uint8_t k = BLACK_KEY[i];
            int pc = (k + 5) % 12;                   /* id 0 = F3 (see the whites) */
            int in_scale = (SC_SET[sc_kind] >> ((pc + 12 - sc_tonic) % 12) & 1);
            int x = x0 + BLACK_LOW[i] * kp + kw - 3; /* straddling the boundary */
            uint16_t col = in_scale ? C_BLACK : C_GRAY;   /* out of scale: the greyed family, same as the whites */
            if (sc_root_key != 0xFF && k == sc_root_key) col = C_AMB;
            cv_rect(x, y0, 7, 24, col);
            if (marked >> k & 1u)                    /* a chord tone (black): its foot */
                cv_rect(x + 1, y0 + 19, 5, 4, C_HI);
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
        /* the component legend spreads the CHORD zone across the full keybed width:
         * the zone's five white columns map to 41px slots (the keybed is too
         * tight for text at 13px), blacks in the upper row, whites below — the
         * left-to-right order still reads along the keys (John: 可读性优先) */
        for (i = 0; i < OC_NCOMP; i++) {
            static const int8_t KSLOT[8] = {0, 0, 1, 1, 2, 3, 3, 4}; /* white col 0..4 */
            static const uint8_t KB[8] = {0, 1, 0, 1, 0, 0, 1, 0};   /* black? */
            int t, tw = 0;                              /* FONT_T: ~5px advance */
            for (t = 0; OC_NAME[i][t]; t++)
                tw += 5;
            tw -= 2;
            int x = x0 + 2 + KSLOT[i] * 41 + 20 - tw / 2;
            int y = y0 + 70 + (KB[i] ? 0 : 15);
            uint16_t col = (sc_mask >> i & 1) ? C_HI : C_GRAY;   /* held = lit */
            cv_text(x, y, &FONT_T, OC_NAME[i], col);
        }
        cv_text(x0 + 2 + 20 - 30, y0 + 100, &FONT_T, "MAJ+DIM = AUG", C_GRAY);
    }
    /* the ACTIVE chord as it will be sent (John 2026-10-29: show the notes
     * so the player can verify OFC is doing what he thinks). Empty = silence. */
    if (sc_ofc && !is_drum_trk()) {
        char chord[22], notes[20];
        char *q = notes;
        uint8_t i, w;
        sc_chord_name_str(chord);
        *q = 0;
        for (i = 0; i < sc_tones_n; i++) {
            const char *nm = sc_tone_name(i);        /* interval-aware spelling:
                                                      * Cm is C Eb G, not C D# G */
            while (*nm) *q++ = *nm++;
            if (i + 1 < sc_tones_n) *q++ = ' ';
        }
        *q = 0;
        if (chord[0]) {                              /* the NAME first (John), then the
                                                      * notes it actually sends. FONT_M:
                                                      * this row is the page's answer, so
                                                      * it gets the room the footer left */
            int32_t cw = text_w(&FONT_M, chord);      /* real advance, not a guess */
            if (6 + cw + 12 + text_w(&FONT_S, notes) > 232) {
                cv_text(6, 190, &FONT_B, chord, C_AMB);
                cv_text(6 + text_w(&FONT_B, chord) + 6, 190, &FONT_T, notes, C_HI);
            } else {
                cv_text(6, 186, &FONT_M, chord, C_AMB);
                cv_text(6 + cw + 12, 190, &FONT_S, notes, C_HI);
            }
        }
    }
    /* The held keys as names (John 2026-10-30: 关闭和弦的时候，弹奏也希望能显示
     * 音名，如果能组成和弦也要显示出来). OFC off only: when it is on, the line
     * above already states the Active Chord -- one answer per screen, not two. */
    if (!sc_ofc && !is_drum_trk()) {
        char chord[22], notes[60];
        if (sc_held_names(chord, notes)) {
            if (chord[0]) {                          /* a nameable set: the name leads */
                int32_t cw = text_w(&FONT_M, chord);
                if (6 + cw + 12 + text_w(&FONT_S, notes) > 232) {
                    cv_text(6, 190, &FONT_B, chord, C_AMB);
                    cv_text(6 + text_w(&FONT_B, chord) + 6, 190, &FONT_T, notes, C_HI);
                } else {
                    cv_text(6, 186, &FONT_M, chord, C_AMB);
                    cv_text(6 + cw + 12, 190, &FONT_S, notes, C_HI);
                }
            } else {                                 /* single note: the name alone is the answer */
                cv_text(6, 186, &FONT_M, notes, C_AMB);
            }
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
        /* John 2026-10-29: one knob, three states — OFF / GATE / LATCH,
         * dead stops both ends (K4 is freed). LATCH->GATE silences at the
         * boundary so no notes stick when the player expects gate. */
        uint8_t prev = sc_ofc;
        if (det < 0 && sc_ofc > 0) sc_ofc--;
        if (det > 0 && sc_ofc < 2) sc_ofc++;
        if (!sc_ofc) {                            /* OFF: full stop, no hanging notes */
            sc_tones_n = 0;
            sc_apply();
            sc_mask = 0;
            sc_gate = 0xFF;
            sc_root_key = 0xFF;
            sc_deg_key = 0xFF;
        } else if (prev == 2 && sc_ofc == 1) {    /* the LATCH->GATE boundary */
            sc_tones_n = 0;
            sc_apply();
            sc_gate = 0xFF;
        }
    }
    kn_poke(enc);
}

static uint8_t scale_button(uint8_t btn)               /* returns 1 = consumed */
{
    if (btn == B_SEL) {
        /* SEL is SCALE's door, and it is a page key like the others now
         * (docs/UI_GRAMMAR.md rule 4): it used to toggle back to the hero, which
         * is the inconsistency John named. Pressing it on the page turns the
         * page; SCALE has one, so it stays. HOME is still the way out. */
        ui_page_key(M_SCALE, ui_mode);
        return 1;
    }
    if (ui_mode != M_SCALE) return 0;
    if (btn == B_HOME || btn == B_SEQ || btn == B_ARP || btn == B_ENV)
        return 0;                                      /* the page keys handle themselves:
                                                        * a door goes where it says, it
                                                        * does not bounce through HOME */
    return 0;                                          /* PLAY/REC/SAVE work as ever */
}
