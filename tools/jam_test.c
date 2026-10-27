/* SPDX-License-Identifier: MPL-2.0 */
/* jam_test.c -- the always-armed realtime ring, engine side.
 *
 * What is under test (jam design v2, John's revisions):
 *   1. capture: a note lands in the ring at the CURRENT 1/16 position, quantized
 *   2. hold: key-up rescales the gate to the steps actually held (round-down, min 1)
 *   3. wrap: notes past 64 steps overwrite the oldest (ring, clock mod 64)
 *   4. clock never stops: playing or stopped, jam_abs advances at the current BPM
 *   5. PLAY from stop does NOT clear the ring (jam-then-listen-then-SAVE is the
 *      main line); it only finalizes holds
 *   6. MERGE adds ring notes into a track with content; same cell+pitch keeps
 *      the track's voice (the ring never overwrites)
 *   7. REPLACE wipes note content but keeps channel/prog identity
 *   8. NEW TRK: first EMPTY melodic track wins; drums (ch 9) are never targets;
 *      no empty track -> -1 and the ring survives
 *   9. merge rescales gates when the target's division is coarser (1/8: a
 *      4-sixteenth hold becomes 2 cells, same seconds)
 *
 * Harness: bpm 120 -> one 1/16 cell = 0.125 s. Ticks are driven in 1/32-cell
 * slices so quantization edges are exercised, not skipped over.
 */
#include "driftseq.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) fails++;
}

static void tick_s(ds_state *s, double secs) {
    const double dt = 1.0 / 1024.0;          /* 1/1024 s slices */
    long n = (long)(secs / dt), k;
    for (k = 0; k < n; k++)
        ds_tick(s, dt);
}

static void setup(ds_state *s) {
    int i;
    ds_init(s, 42);
    for (i = 0; i < DS_TRACKS; i++)
        ds_clear_track(&s->pat.track[i]);
    s->pat.track[0].channel = 9;             /* drums */
    s->pat.track[5].channel = 0;
    s->pat.track[5].division = DS_DIV_16;
    s->pat.track[5].prog = 38;
    ds_voice_add(&s->pat.track[5], 0, 40, 90, 1);   /* track 5 has content at step 0 */
    s->pat.track[6].channel = 0;
    s->pat.track[6].division = DS_DIV_8;     /* coarse: 2 sixteenths per cell */
    s->pat.track[6].prog = 81;
    ds_voice_add(&s->pat.track[6], 0, 41, 90, 1);   /* track 6 has content too */
}

int main(void) {
    ds_state s;

    /* ---- 1+2: capture + hold ---- */
    setup(&s);
    check(!ds_jam_holds_notes(&s), "ring starts empty");
    tick_s(&s, 0.0625);                      /* half a cell: still slot 0 */
    ds_jam_capture(&s, 60, 77, 1);
    check(s.jam.steps[0].v[0].note == 60 && s.jam.steps[0].v[0].vel == 77,
          "note lands quantized in the current slot");
    check(s.jam.steps[0].v[0].gate == 1, "provisional gate is 1 at key-down");
    tick_s(&s, 0.25);                        /* two more cells: jam_abs = 2 */
    ds_jam_capture(&s, 60, 77, 0);
    check(s.jam.steps[0].v[0].gate == 3, "hold of ~3 cells finalizes gate 3 on key-up");
    ds_jam_capture(&s, 62, 100, 1);
    tick_s(&s, 0.01);
    ds_jam_capture(&s, 62, 100, 0);
    check(s.jam.steps[2].v[0].note == 62 && s.jam.steps[2].v[0].gate == 1,
          "sub-cell blip still records, gate min 1");

    /* ---- 3: wrap overwrites ---- */
    ds_jam_reset(&s);
    tick_s(&s, 0.125 * 65);                  /* 65 cells: slot 1 */
    ds_jam_capture(&s, 64, 55, 1);
    ds_jam_capture(&s, 64, 55, 0);
    check(s.jam.steps[1].nvoices == 1 && s.jam.steps[1].v[0].note == 64,
          "past 64 steps the ring wraps to slot 1");

    /* ---- 4: the clock runs while stopped AND while playing ---- */
    setup(&s);
    {
        uint32_t a0 = s.jam_abs;
        tick_s(&s, 0.5);                     /* 4 cells, stopped */
        check(s.jam_abs == a0 + 4, "stop: ring clock walks at BPM");
        ds_play(&s);
        tick_s(&s, 0.5);
        check(s.jam_abs == a0 + 8, "play: ring clock keeps walking");
    }

    /* ---- 5: PLAY keeps the take ---- */
    setup(&s);
    ds_jam_capture(&s, 55, 70, 1);
    ds_jam_capture(&s, 55, 70, 0);
    check(ds_jam_holds_notes(&s), "ring holds the take before PLAY");
    ds_play(&s);
    check(ds_jam_holds_notes(&s), "PLAY from stop keeps the take (jam-listen-save)");
    check(s.jam_held_n == 0, "PLAY finalizes held notes");

    /* ---- 6: MERGE adds without overwriting ---- */
    setup(&s);
    tick_s(&s, 0.125 * 3);                   /* slot 3 */
    ds_jam_capture(&s, 47, 64, 1);
    ds_jam_capture(&s, 47, 64, 0);
    ds_jam_save_merge(&s, 5);                /* track 5 already has note 40 at step 0 */
    check(s.pat.track[5].steps[3].nvoices == 1 && s.pat.track[5].steps[3].v[0].note == 47,
          "MERGE lands the ring note in the target track");
    check(s.pat.track[5].steps[0].nvoices == 1 && s.pat.track[5].steps[0].v[0].note == 40,
          "MERGE keeps the track's own voice (no overwrite)");
    check(!ds_jam_holds_notes(&s), "MERGE clears the ring");
    /* second merge of the same content: dedupe makes it idempotent only while
     * the ring is empty — an empty ring merging is a no-op */
    tick_s(&s, 0.125 * 3);
    ds_jam_capture(&s, 47, 64, 1);
    ds_jam_capture(&s, 47, 64, 0);
    ds_jam_save_merge(&s, 5);
    check(s.pat.track[5].steps[3].nvoices == 1, "re-MERGE same pitch = still one voice (-2 dedupe)");

    /* ---- 7: REPLACE wipes content, keeps identity ---- */
    setup(&s);
    ds_jam_capture(&s, 72, 90, 1);
    ds_jam_capture(&s, 72, 90, 0);
    ds_jam_save_replace(&s, 5);
    check(s.pat.track[5].steps[0].nvoices == 1 && s.pat.track[5].steps[0].v[0].note == 72,
          "REPLACE puts the ring in the target");
    check(s.pat.track[5].prog == 38 && s.pat.track[5].channel == 0,
          "REPLACE keeps the target's prog and channel");
    check(!ds_jam_holds_notes(&s), "REPLACE clears the ring");

    /* ---- 8: NEW TRK target choice ---- */
    setup(&s);
    {
        uint8_t t = 0xFF;
        ds_jam_capture(&s, 50, 80, 1);
        ds_jam_capture(&s, 50, 80, 0);
        check(ds_jam_save_new(&s, &t) == 0 && t == 1,
              "NEW TRK picks the first empty melodic track (1: drum 0 skipped, 5/6 have content)");
        /* now EVERY melodic track has content -> -1, ring survives */
        {
            int i;
            for (i = 1; i < DS_TRACKS; i++)
                if (s.pat.track[i].channel != 9)
                    ds_voice_add(&s.pat.track[i], (uint8_t)(i + 7), 60 + i, 80, 1);
        }
        tick_s(&s, 0.125 * 2);
        ds_jam_capture(&s, 51, 80, 1);
        ds_jam_capture(&s, 51, 80, 0);
        t = 0xFF;
        check(ds_jam_save_new(&s, &t) == -1, "no empty melodic track -> -1");
        check(ds_jam_holds_notes(&s), "failed NEW TRK keeps the ring");
    }

    /* ---- 9: division rescale on MERGE (target = 1/8, ring = 1/16) ---- */
    setup(&s);
    ds_jam_capture(&s, 45, 80, 1);           /* slot 0, hold 4 sixteenths */
    tick_s(&s, 0.5);
    ds_jam_capture(&s, 45, 80, 0);
    check(s.jam.steps[0].v[0].gate == 4, "4-sixteenth hold recorded as gate 4");
    ds_jam_save_merge(&s, 6);                /* track 6 is 1/8: 2 sixteenths/cell */
    check(s.pat.track[6].steps[0].nvoices == 2 &&
          s.pat.track[6].steps[0].v[1].gate == 2,
          "MERGE into 1/8 rescales gate 4 sixteenths -> 2 cells (same seconds)");

    printf("%s\n", fails ? "JAM TEST FAILED" : "JAM TEST PASSED");
    return fails ? 1 : 0;
}
