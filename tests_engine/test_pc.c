/* SPDX-License-Identifier: MPL-2.0 */
/* test_pc.c — who owns a channel's program, and when the wire hears about it.
 *
 * The receiver keeps ONE program per channel, so a program change is channel
 * state, not track state. These tests state the contract the pattern bank
 * (chain) will lean on: the owner is a property of the pattern, re-derived at
 * every tick, and a switch is nothing more than the pattern changing under the
 * same tick loop.
 */
#include "driftseq.h"
#include "demo_pattern.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } \
                              else { printf("ok "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Walk one tick's byte stream, collect (channel, program) pairs. */
static int take_pcs(const ds_state *s, uint8_t *ch_out, uint8_t *prog_out, int cap)
{
    uint32_t i = 0;
    int n = 0;
    while (i < s->out_len) {
        uint8_t b = s->out[i];
        if (b < 0x80) { i++; continue; }
        if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) {
            if (n < cap && (b & 0xF0) == 0xC0) { ch_out[n] = b & 0x0F; prog_out[n] = s->out[i + 1]; n++; }
            i += 2;
            continue;
        }
        i += 3;
    }
    return n;
}

static void add_note(ds_track *t, uint32_t step, uint8_t note)
{
    ds_voice_add(t, step, note, 100, 1);
}

/* Run n ticks, gathering every PC the engine emits into `got`
 * (channel<<8 | program), deduplicated by order of appearance. */
static int run_collect(ds_state *s, double dt, long nticks, int *got, int cap)
{
    long k;
    int n = 0;
    for (k = 0; k < nticks; k++) {
        uint8_t c[16], p[16];
        int m, j;
        ds_tick(s, dt);
        m = take_pcs(s, c, p, 16);
        for (j = 0; j < m; j++) {
            if (n < cap) got[n] = (c[j] << 8) | p[j];
            n++;
        }
    }
    return n;
}

/* 1. The demo: one PC per used channel, carrying the owner's program.
 *    Before the owner rule this emitted 14 — eleven empty tracks each sent
 *    prog 0 on ch0 and overwrote the bass. */
static void test_demo_one_pc_per_channel(void)
{
    ds_state s;
    int got[64];
    int n;
    ds_load_demo(&s);
    ds_play(&s);
    n = run_collect(&s, 64.0 / 44100.0, 2000, got, 64);
    CHECK(n == 3, "demo sends one program change per used channel (%d, want 3)", n);
    CHECK((got[0] >> 8) == 9 && (got[0] & 0xFF) == 0, "ch9 (drums) gets prog 0, got ch%d prog%d",
          got[0] >> 8, got[0] & 0xFF);
    CHECK((got[1] >> 8) == 0 && (got[1] & 0xFF) == 38, "ch0 gets the bass owner's prog 38, got ch%d prog%d",
          got[1] >> 8, got[1] & 0xFF);
    CHECK((got[2] >> 8) == 1 && (got[2] & 0xFF) == 81, "ch1 gets the lead owner's prog 81, got ch%d prog%d",
          got[2] >> 8, got[2] & 0xFF);
}

/* 2. An empty track sharing a channel must not steal the program — and must
 *    not send anything of its own either. */
static void test_empty_track_does_not_steal(void)
{
    ds_state s;
    int got[64];
    int n;
    ds_init(&s, 7);
    s.pat.track[2].channel = 0;  s.pat.track[2].prog = 55;   /* empty, higher index */
    s.pat.track[9].channel = 0;  s.pat.track[9].prog = 77;   /* empty, lower index than the player */
    s.pat.track[12].channel = 0; s.pat.track[12].prog = 20;
    s.pat.track[12].loop_len = 4;
    add_note(&s.pat.track[12], 0, 60);
    ds_play(&s);
    n = run_collect(&s, 64.0 / 44100.0, 400, got, 64);
    CHECK(n == 1 && (got[0] >> 8) == 0 && (got[0] & 0xFF) == 20,
          "only the track with notes speaks for ch0 (%d sends, ch%d prog%d)", n,
          n ? got[0] >> 8 : -1, n ? got[0] & 0xFF : -1);
}

/* 3. Lowest index wins among tracks that DO have notes. */
static void test_lowest_index_owner(void)
{
    ds_state s;
    int got[64];
    int n;
    ds_init(&s, 7);
    s.pat.track[3].channel = 5;  s.pat.track[3].prog = 40; s.pat.track[3].loop_len = 4;
    add_note(&s.pat.track[3], 0, 60);
    s.pat.track[8].channel = 5;  s.pat.track[8].prog = 90; s.pat.track[8].loop_len = 4;
    add_note(&s.pat.track[8], 0, 64);
    ds_play(&s);
    n = run_collect(&s, 64.0 / 44100.0, 400, got, 64);
    CHECK(n == 1 && (got[0] & 0xFF) == 40,
          "the lower-numbered busy track owns the channel (%d sends, prog%d)",
          n, n ? got[0] & 0xFF : -1);
}

/* 4. Muting the owner hands the channel to the next track that has notes,
 *    and the receiver is told (that is the audible part). */
static void test_mute_owner_hands_over(void)
{
    ds_state s;
    int got[64];
    int n;
    ds_init(&s, 7);
    s.pat.track[1].channel = 3; s.pat.track[1].prog = 11; s.pat.track[1].loop_len = 4;
    add_note(&s.pat.track[1], 0, 60);
    s.pat.track[4].channel = 3; s.pat.track[4].prog = 22; s.pat.track[4].loop_len = 4;
    add_note(&s.pat.track[4], 0, 64);
    ds_play(&s);
    run_collect(&s, 64.0 / 44100.0, 100, got, 64);
    s.pat.track[1].mute = 1;
    n = run_collect(&s, 64.0 / 44100.0, 100, got, 64);
    CHECK(n == 1 && (got[0] >> 8) == 3 && (got[0] & 0xFF) == 22,
          "muting the owner re-states the channel with the heir's program (%d sends, prog%d)",
          n, n ? got[0] & 0xFF : -1);
    /* and unmuting brings the original back */
    s.pat.track[1].mute = 0;
    n = run_collect(&s, 64.0 / 44100.0, 100, got, 64);
    CHECK(n == 1 && (got[0] & 0xFF) == 11,
          "unmuting re-states the original owner's program (prog%d)", n ? got[0] & 0xFF : -1);
}

/* 5. The same program is never re-sent tick after tick: the receiver already
 *    agrees, and a PC at every tick would retune/pop on real modules. */
static void test_no_repeat(void)
{
    ds_state s;
    int got[64];
    int n;
    ds_init(&s, 7);
    s.pat.track[0].channel = 2; s.pat.track[0].prog = 33; s.pat.track[0].loop_len = 4;
    add_note(&s.pat.track[0], 0, 60);
    ds_play(&s);
    n = run_collect(&s, 64.0 / 44100.0, 5000, got, 64);
    CHECK(n == 1, "a steady channel is told its program exactly once (%d)", n);
}

/* 6. Pattern switch, the reason any of this matters: the new pattern's owner
 *    may be a different track with a different program. Swapping the pattern
 *    under a running transport must re-state the channel. */
static void test_pattern_switch(void)
{
    ds_state a, b;
    int got[64];
    int n;
    ds_init(&a, 7);
    ds_init(&b, 7);
    /* pattern A: ch4 = prog 5, owner is track 2 */
    a.pat.track[2].channel = 4; a.pat.track[2].prog = 5; a.pat.track[2].loop_len = 4;
    add_note(&a.pat.track[2], 0, 60);
    /* pattern B: same channel, a different track owns it, different program */
    b.pat.track[6].channel = 4; b.pat.track[6].prog = 63; b.pat.track[6].loop_len = 4;
    add_note(&b.pat.track[6], 0, 72);

    ds_play(&a);
    n = run_collect(&a, 64.0 / 44100.0, 100, got, 64);
    CHECK(n == 1 && (got[0] & 0xFF) == 5, "pattern A states ch4 = prog 5");
    a.pat = b.pat;                                     /* the switch, as a chain will do it */
    n = run_collect(&a, 64.0 / 44100.0, 100, got, 64);
    CHECK(n == 1 && (got[0] & 0xFF) == 63,
          "after switching to B the channel is told 63 (%d sends, prog%d)", n,
          n ? got[0] & 0xFF : -1);
}

/* 7. A channel nobody has notes on gets no program change at all. */
static void test_silent_channel_silent(void)
{
    ds_state s;
    int got[64];
    int n;
    ds_init(&s, 7);
    /* every track keeps prog 0, but none has notes */
    ds_play(&s);
    n = run_collect(&s, 64.0 / 44100.0, 400, got, 64);
    CHECK(n == 0, "an empty pattern sends no program change at all (%d)", n);
}

int main(void)
{
    fails = 0;
    test_demo_one_pc_per_channel();
    test_empty_track_does_not_steal();
    test_lowest_index_owner();
    test_mute_owner_hands_over();
    test_no_repeat();
    test_pattern_switch();
    test_silent_channel_silent();
    if (fails) { printf("\n%d PROGRAM CHANGE TEST(S) FAILED\n", fails); return 1; }
    printf("\nPROGRAM CHANGE TESTS PASSED\n");
    return 0;
}
