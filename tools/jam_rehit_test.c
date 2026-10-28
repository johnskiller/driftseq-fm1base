/* jam ring take semantics: pass boundary + re-hit replace.
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

    /* 1. same-cell chord + immediate re-hit: chord mate survives, re-hit wins */
    to_slot(&s, 8);
    ds_jam_capture(&s, 60, 100, 1);          /* C + E in the same cell (a chord) */
    ds_jam_capture(&s, 64, 90, 1);
    tick_s(&s, 0.130);
    ds_jam_capture(&s, 60, 0, 0);
    ds_jam_capture(&s, 64, 0, 0);
    to_slot(&s, 8);                          /* wraps to the next pass: slot cleared */
    ds_jam_capture(&s, 60, 70, 1);
    ds_jam_capture(&s, 60, 0, 0);
    ds_jam_capture(&s, 64, 90, 1);           /* E again, same pass, same slot */
    ds_jam_capture(&s, 64, 0, 0);
    ds_jam_capture(&s, 60, 55, 1);           /* C re-hit in the same cell */
    ds_jam_capture(&s, 60, 0, 0);
    {   const ds_step *st = &s.jam.steps[8];
        check(st->nvoices == 2, "re-hit keeps the chord mate (2 voices)");
        check(st->nvoices == 2 && st->v[0].note == 64 && st->v[0].vel == 90,
              "chord mate E untouched");
        check(st->nvoices == 2 && st->v[1].note == 60 && st->v[1].vel == 55,
              "re-hit C rewritten at vel 55");
    }

    /* 2. John's scenario: C E G, a full pass later A B A B -> only A B A B */
    ds_init(&s, 1);
    s.bpm = 480.0;
    to_slot(&s, 0);
    ds_jam_capture(&s, 60, 90, 1); ds_jam_capture(&s, 60, 0, 0);   /* C slot 0 */
    tick_s(&s, 0.03125);
    ds_jam_capture(&s, 64, 90, 1); ds_jam_capture(&s, 64, 0, 0);   /* E slot 1 */
    tick_s(&s, 0.03125);
    ds_jam_capture(&s, 67, 90, 1); ds_jam_capture(&s, 67, 0, 0);   /* G slot 2 */
    {   unsigned a;
        for (a = 0; a < 70; a++) tick_s(&s, 0.03125);              /* 70 cells: past the pass */
    }
    ds_jam_capture(&s, 69, 80, 1); ds_jam_capture(&s, 69, 0, 0);   /* A slot 8 */
    tick_s(&s, 0.03125);
    ds_jam_capture(&s, 71, 80, 1); ds_jam_capture(&s, 71, 0, 0);   /* B slot 9 */
    ds_jam_capture(&s, 69, 80, 1); ds_jam_capture(&s, 69, 0, 0);   /* A slot 9 */
    tick_s(&s, 0.03125);
    ds_jam_capture(&s, 71, 80, 1); ds_jam_capture(&s, 71, 0, 0);   /* B slot 10 */
    check(s.jam.steps[0].nvoices == 1 && s.jam.steps[0].v[0].note == 60,
          "stale slot 0 (C) still sits in the ring until overwritten");
    check(s.jam.steps[8].nvoices == 1 && s.jam.steps[8].v[0].note == 69,
          "current pass: A at slot 8");
    /* MERGE onto a fresh track: stale slots must be skipped, current pass kept */
    {   ds_state s2;
        ds_init(&s2, 1);
        s2.bpm = 480.0;
        to_slot(&s2, 0);
        ds_jam_capture(&s2, 60, 90, 1); ds_jam_capture(&s2, 60, 0, 0);   /* C, pass 0 */
        {   unsigned a;
            for (a = 0; a < 70; a++) tick_s(&s2, 0.03125);               /* into pass 1 */
        }
        ds_jam_capture(&s2, 69, 80, 1); ds_jam_capture(&s2, 69, 0, 0);   /* A, pass 1 */
        ds_jam_save_merge(&s2, 1);         /* target track 1 (empty) */
        check(s2.pat.track[1].steps[0].nvoices == 0,
              "MERGE skips the stale slot (C from the old pass is NOT merged)");
        check(s2.pat.track[1].steps[6].nvoices == 1 && s2.pat.track[1].steps[6].v[0].note == 69,
              "MERGE lands the current-pass A at slot 6");
    }

    /* 3. pre-wrap quick re-hit (drum-roll): second hit replaces, one voice */
    ds_state t2;
    ds_init(&t2, 1);
    t2.bpm = 480.0;
    to_slot(&t2, 20);
    ds_jam_capture(&t2, 38, 100, 1);
    tick_s(&t2, 0.010);
    ds_jam_capture(&t2, 38, 0, 0);
    ds_jam_capture(&t2, 38, 55, 1);
    ds_jam_capture(&t2, 38, 0, 0);
    {   const ds_step *st = &t2.jam.steps[20];
        check(st->nvoices == 1 && st->v[0].vel == 55,
              "pre-wrap roll: second hit replaces (vel 55, one voice)");
    }

    printf(fails ? "REHIT: %d FAILURES\n" : "REHIT: all passed\n", fails);
    return fails != 0;
}
