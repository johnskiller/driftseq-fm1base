/* re-hit semantics in the jam ring: replace same pitch, keep chord mates.
 * build: cc -std=c99 -O2 -Iengine -I. tools/jam_rehit_test.c engine/driftseq.c */
#include <stdio.h>
#include <string.h>
#include "driftseq.h"

static int fails;
static void check(int ok, const char *what) {
    printf("  %s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) fails++;
}
static void tick_s(ds_state *s, double sec) { ds_tick(s, sec); }
static void to_slot(ds_state *s, unsigned slot) { while (ds_jam_pos(s) != slot) tick_s(s, 0.001); }

int main(void) {
    ds_state s;
    ds_init(&s, 1);
    s.bpm = 480.0;                           /* 1/16 = 31.25 ms: fast sim clock */

    /* 1. first pass: note 60 at slot 8, vel 100, held 4 cells */
    to_slot(&s, 8);
    ds_jam_capture(&s, 60, 100, 1);
    tick_s(&s, 0.130);
    ds_jam_capture(&s, 60, 0, 0);
    {   const ds_step *st = &s.jam.steps[8];
        check(st->nvoices == 1 && st->v[0].note == 60 && st->v[0].vel == 100 && st->v[0].gate >= 3,
              "first pass: note 60 vel 100 long gate at slot 8");
    }

    /* 2. wrap fully past slot 8 (64+ steps) and re-hit the same pitch, vel 40 */
    to_slot(&s, 8);                          /* 64 cells later: same slot, next pass */
    {   unsigned before = s.jam_abs;
        ds_jam_capture(&s, 60, 40, 1);
        ds_jam_capture(&s, 60, 0, 0);
        check(before >= 64, "the ring really wrapped before the re-hit");
    }
    {   const ds_step *st = &s.jam.steps[8];
        check(st->nvoices == 1, "re-hit after wrap: still ONE voice (replace, no overdub stack)");
        check(st->nvoices == 1 && st->v[0].vel == 40,
              "re-hit after wrap: the NEW hit's velocity wins");
        check(st->nvoices == 1 && st->v[0].gate == 1,
              "re-hit after wrap: the NEW hit's gate wins (short tap)");
    }

    /* 3. chord mate: a different pitch in the slot survives the re-hit */
    ds_jam_capture(&s, 64, 90, 1);           /* E lands at slot 8 too (same cell) */
    ds_jam_capture(&s, 64, 0, 0);
    to_slot(&s, 8);                          /* full wrap back to slot 8 */
    ds_jam_capture(&s, 60, 70, 1);
    ds_jam_capture(&s, 60, 0, 0);
    {   const ds_step *st = &s.jam.steps[8];
        check(st->nvoices == 2, "re-hit keeps the chord mate (2 voices)");
        check(st->nvoices == 2 && st->v[0].note == 64 && st->v[0].vel == 90,
              "chord mate E untouched");
        check(st->nvoices == 2 && st->v[1].note == 60 && st->v[1].vel == 70,
              "re-hit C rewritten at vel 70");
    }

    /* 4. pre-wrap quick re-hit (drum-roll style): down/up/down within a slot */
    ds_state t2;
    ds_init(&t2, 1);
    t2.bpm = 480.0;
    to_slot(&t2, 20);
    ds_jam_capture(&t2, 38, 100, 1);
    tick_s(&t2, 0.010);                      /* still slot 20 */
    ds_jam_capture(&t2, 38, 0, 0);
    ds_jam_capture(&t2, 38, 55, 1);          /* roll: second hit, same slot */
    ds_jam_capture(&t2, 38, 0, 0);
    {   const ds_step *st = &t2.jam.steps[20];
        check(st->nvoices == 1 && st->v[0].vel == 55,
              "pre-wrap roll: second hit replaces (vel 55, one voice)");
    }

    printf(fails ? "REHIT: %d FAILURES\n" : "REHIT: all passed\n", fails);
    return fails != 0;
}
