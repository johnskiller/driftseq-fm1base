/* SPDX-License-Identifier: MPL-2.0 */
/* tie_test.c -- what a tie does to the MIDI stream, and what the step flags
 * that predate it (on, nvoices) still mean.
 *
 * The rules under test, in the order the engine applies them:
 *   1. a tie step never attacks: no note-on of its own -- so putting a tie on a
 *      step that WOULD have attacked removes exactly that attack
 *   2. a tie step extends the previous attack: the note-off arrives later by
 *      the tie run's length in cells
 *   3. a tie is NOT a rest: the sound carries through the step instead of
 *      stopping and restarting (that is the whole point -- "play 时是连续音")
 *   4. on stays what it was: bare gate (on=1, nvoices=0) still draws and still
 *      does not sound; silent-keep (on=0, voices present) still holds its data
 *
 * Harness: bpm 120, division 16th (cell = 0.125 s), an 8-step loop with an
 * attack on every even step (0,2,4,6). So one pass = 4 attacks, 2 s = 2 passes
 * = 8 attacks, and each tie placed on an ODD-numbered-of-those step removes one.
 */
#include "driftseq.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) fails++;
}

static void setup(ds_state *s) {
    int i;
    ds_init(s, 42);                          /* bpm 120, rng seeded */
    for (i = 0; i < DS_TRACKS; i++)
        ds_clear_track(&s->pat.track[i]);
    s->pat.track[0].loop_len = 8;
    s->pat.track[0].loop_start = 0;
    s->pat.track[0].division = DS_DIV_16;
    s->pat.track[0].channel = 0;
    for (i = 0; i < 8; i += 2)               /* steps 0,2,4,6 attack */
        ds_voice_add(&s->pat.track[0], (uint8_t)i, 60, 100, 1);
    ds_play(s);
}

/* ds_tick resets out_len every call, so the stream must be consumed per tick.
 * The stream mixes 2-byte messages (0xC0 program change) with 3-byte ones
 * (0x90 note-on, 0x80 note-off), so it must be parsed, not strided. */
struct got { int ons, offs; double first_off; };
static struct got run(ds_state *s, double secs) {
    const double dt = 64.0 / 44100.0;
    long iters = (long)(secs / dt), k;
    struct got g = {0, 0, -1.0};
    for (k = 0; k < iters; k++) {
        double t = (double)k * dt;
        uint32_t i = 0;
        ds_tick(s, dt);
        while (i < s->out_len) {
            uint8_t st = s->out[i] & 0xF0;
            uint8_t d2 = (i + 2 < s->out_len) ? s->out[i + 2] : 0;
            if (st == 0x90 && d2) {
                g.ons++;
                i += 3;
            } else if (st == 0x80 || st == 0x90) {
                g.offs++;
                if (g.first_off < 0.0) g.first_off = t;
                i += 3;
            } else if (st == 0xC0 || st == 0xD0) {
                i += 2;                       /* one data byte */
            } else {
                i += 3;
            }
        }
    }
    return g;
}

int main(void) {
    ds_state s;
    struct got plain, tied, three;
    const double cell = 60.0 / 120.0 / 4.0;   /* bpm 120, 16th = 0.125 s */

    /* --- 1. gate 1, no tie: 4 attacks a pass, released after ~1 cell --- */
    setup(&s);
    plain = run(&s, 2.0);
    check(plain.ons == 8, "plain: 8 attacks in 2 s (4 per 8-step pass, 2 passes)");
    check(plain.offs == 8, "plain: every attack is released");
    check(plain.first_off > 0.0 && plain.first_off < cell * 1.5 + 0.02,
          "plain: the first release lands about one cell after its attack");

    /* --- 2. tie ON an attacking step (2): that attack disappears --- */
    setup(&s);
    s.pat.track[0].steps[2].tie = 1;
    tied = run(&s, 2.0);
    check(tied.ons == 6, "tie(step2): the attack on step 2 disappears -> 3 per pass");
    check(tied.offs == 6, "tie(step2): no note is left hanging");
    check(tied.first_off < plain.first_off + cell * 0.5,
          "tie(step2): step 0 is NOT extended -- step 1 is not a tie, so step 0\n"
          "           closed normally and step 2's tie has nothing to hold");

    /* --- 3. tie on a step that did not attack is only an extension --- */
    setup(&s);
    s.pat.track[0].steps[1].tie = 1;         /* step 1 had no voice */
    {
        struct got g = run(&s, 2.0);
        check(g.ons == plain.ons, "tie(step1): no attack lost, it had none to lose");
        check(g.first_off > plain.first_off + cell * 0.5,
              "tie(step1): but it still extends step 0's note");
    }

    /* --- 4. a contiguous run of three ties from step 1: 4 cells, one attack lost */
    setup(&s);
    s.pat.track[0].steps[1].tie = 1;
    s.pat.track[0].steps[2].tie = 1;         /* swallows step 2's attack */
    s.pat.track[0].steps[3].tie = 1;
    three = run(&s, 2.0);
    check(three.ons == 6, "tie(1,2,3): step 2's attack is swallowed -> 3 per pass");
    check(three.first_off > plain.first_off + cell * 2.5,
          "tie(1,2,3): step 0's note spans the whole tied run (4 cells)");
    check(three.first_off > tied.first_off,
          "tie(1,2,3): longer than a single tie, so the run accumulates");

    /* --- 4b. a contiguous run that swallows a real attack --- */
    setup(&s);
    s.pat.track[0].steps[3].tie = 1;
    s.pat.track[0].steps[4].tie = 1;
    {
        struct got g = run(&s, 2.0);
        check(g.ons == 6, "tie(3,4): step 4's attack is swallowed -> 3 per pass");
        check(g.first_off < plain.first_off + cell * 0.5,
              "tie(3,4): step 2 still closed normally (step 3 came too late to it)");
    }

    /* --- 5. on=1 with no voices is still a bare gate: draws, never sounds --- */
    setup(&s);
    ds_voice_remove(&s.pat.track[0], 0, 60);
    s.pat.track[0].steps[0].on = 1;          /* the euclid skeleton latches this */
    plain = run(&s, 1.0);
    check(plain.ons == 3, "bare gate (on=1, no voices): silent itself, others fire");
    check(ds_step_active(&s.pat.track[0], 0) == 0,
          "bare gate: ds_step_active still reports 0 (it needs a voice)");

    /* --- 6. on=0 with voices is still silent-keep: data survives, no sound --- */
    setup(&s);
    s.pat.track[0].steps[0].on = 0;
    check(s.pat.track[0].steps[0].nvoices == 1, "silent-keep: the voice is still there");
    plain = run(&s, 1.0);
    check(plain.ons == 3, "silent-keep: step 0 drops out, the other three fire");

    /* --- 7. a tie at the loop start with nothing before it must not hang --- */
    setup(&s);
    s.pat.track[0].steps[0].tie = 1;
    plain = run(&s, 1.0);
    check(plain.ons == 3 && plain.offs == 3,
          "orphan tie at the loop start: swallowed, no flood and no stuck note");

    printf(fails ? "\nTIE TESTS FAILED (%d)\n" : "\nTIE TESTS PASSED\n", fails);
    return fails != 0;
}
