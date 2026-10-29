/* SPDX-License-Identifier: MPL-2.0 */
/* test_trkset.c — the three track-settings fields that change what the engine
 * emits: ROUTE (the wire/GM masks), VOICE (mono collapses to the top pitch and
 * offs what it held), and SWING (global x per-track weight). Plain asserts,
 * cc -std=c99 -O2; exit 0 = PASS.
 *
 * These are the fields the Track Settings page edits. A page that writes a
 * field nothing reads is worse than no page, so each one gets a behavioural
 * test here, not just a round-trip through the packer. */
#include "driftseq.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FAIL(...) do { printf("FAIL: " __VA_ARGS__); printf("\n"); exit(1); } while (0)
#define OK(...)    do { printf("ok " __VA_ARGS__); printf("\n"); } while (0)

/* collect one tick's note-on / note-off notes on a channel */
static void scan(const ds_state *s, uint8_t ch, int *on_lo, int *on_hi, int *off_n, int *on_n) {
    uint32_t i = 0;
    *on_lo = 127; *on_hi = -1; *off_n = 0; *on_n = 0;
    while (i < s->out_len) {
        uint8_t b = s->out[i];
        if (b < 0x80) { i++; continue; }
        if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) { i += 2; continue; }
        if (i + 2 >= s->out_len) break;
        if ((b & 0x0F) == ch) {
            uint8_t n = s->out[i + 1], v = s->out[i + 2];
            if ((b & 0xF0) == 0x90 && v > 0) {
                if (n < *on_lo) *on_lo = n;
                if (n > *on_hi) *on_hi = n;
                (*on_n)++;
            } else if ((b & 0xF0) == 0x80 || ((b & 0xF0) == 0x90 && v == 0)) {
                (*off_n)++;
            }
        }
        i += 3;
    }
}

/* one track, one step at loop start, held for a whole cell; step 4 has a
 * single voice so a mono run visits two different notes in a row. */
static void one_track(ds_state *s, uint8_t ch) {
    int i;
    ds_init(s, 7);
    for (i = 0; i < DS_TRACKS; i++)
        s->pat.track[i].loop_len = 1;            /* park the others: they get the same ch 0 */
    s->pat.track[0].channel = ch;
    s->pat.track[0].loop_len = 8;
    s->pat.track[0].loop_start = 0;
    s->pat.track[0].division = DS_DIV_16;
    s->pat.track[0].steps[0].nvoices = 0;        /* clear the demo */
    s->pat.track[0].steps[0].on = 0;
    ds_clear_track(&s->pat.track[0]);
    s->pat.track[0].channel = ch;
    s->pat.track[0].loop_len = 8;
    ds_voice_add(&s->pat.track[0], 0, 60, 100, 1);
    ds_voice_add(&s->pat.track[0], 0, 64, 100, 1);
    ds_voice_add(&s->pat.track[0], 0, 67, 100, 1);   /* a C major triad on step 0 */
    ds_voice_add(&s->pat.track[0], 4, 55, 100, 1);
}

static void test_route_masks(void) {
    ds_state s;
    one_track(&s, 5);
    s.pat.track[0].route = DS_ROUTE_GM;
    ds_route_sync(&s);
    if (!(s.gm_mask & (1u << 5))) FAIL("ROUTE=GM must keep ch5 on the GM side");
    if (s.wire_mask & (1u << 5)) FAIL("ROUTE=GM must take ch5 off the wire");
    s.pat.track[0].route = DS_ROUTE_USB;
    ds_route_sync(&s);
    if (!(s.wire_mask & (1u << 5))) FAIL("ROUTE=USB must put ch5 on the wire");
    if (s.gm_mask & (1u << 5)) FAIL("ROUTE=USB must take ch5 off GM");
    s.pat.track[0].route = DS_ROUTE_BOTH;
    ds_route_sync(&s);
    if (!(s.wire_mask & (1u << 5)) || !(s.gm_mask & (1u << 5)))
        FAIL("ROUTE=BOTH must be on both sides");
    OK("route masks: GM / USB / BOTH each set exactly their own side");

    /* two tracks on one channel: either one asking for the wire wins */
    s.pat.track[1].channel = 5;
    s.pat.track[1].route = DS_ROUTE_USB;
    s.pat.track[0].route = DS_ROUTE_GM;
    ds_route_sync(&s);
    if (!(s.wire_mask & (1u << 5))) FAIL("a second track routed to USB must re-open the channel");
    if (!(s.gm_mask & (1u << 5))) FAIL("the GM track keeps its side of a shared channel");
    OK("route masks: a shared channel ORs both tracks");
}

static void test_mono(void) {
    ds_state s, p;
    int lo, hi, offs, ons, i;
    double dt = 64.0 / 44100.0;

    /* POLY: step 0 fires all three notes */
    one_track(&p, 5);
    p.pat.track[0].voice_mode = DS_VOICE_POLY;
    ds_play(&p);
    ds_tick(&p, dt);
    scan(&p, 5, &lo, &hi, &offs, &ons);
    if (ons != 3 || lo != 60 || hi != 67)
        FAIL("poly step 0 must fire 3 notes 60..67 (got %d, %d..%d)", ons, lo, hi);
    OK("voice poly: a 3-note step fires 3 notes");

    /* MONO: only the top pitch sounds */
    one_track(&s, 5);
    s.pat.track[0].voice_mode = DS_VOICE_MONO;
    ds_play(&s);
    ds_tick(&s, dt);
    scan(&s, 5, &lo, &hi, &offs, &ons);
    if (ons != 1 || hi != 67)
        FAIL("mono step 0 must fire exactly the top pitch 67 (got %d notes, top %d)", ons, hi);
    OK("voice mono: a 3-note step fires only its top pitch (67)");

    /* The hang this guards: step 0's gate (6 steps) outlives step 4's attack.
     * Without an explicit off, note 67 has no off anywhere in the stream —
     * its gate entry was the only one and it is not the note mono now holds. */
    one_track(&s, 5);
    s.pat.track[0].voice_mode = DS_VOICE_MONO;
    ds_clear_track(&s.pat.track[0]);
    s.pat.track[0].channel = 5;
    s.pat.track[0].loop_len = 8;
    s.pat.track[0].voice_mode = DS_VOICE_MONO;
    ds_voice_add(&s.pat.track[0], 0, 60, 100, 6);    /* gate 6 steps: still sounding at step 4 */
    ds_voice_add(&s.pat.track[0], 0, 67, 100, 6);
    ds_voice_add(&s.pat.track[0], 4, 55, 100, 1);
    ds_play(&s);
    {
        int off67_seen = 0, on55_seen = 0, order_ok = 0;
        for (i = 0; i < 4000 && !(off67_seen && on55_seen); i++) {
            uint32_t k = 0;
            ds_tick(&s, dt);
            while (k < s.out_len) {
                uint8_t b = s.out[k];
                if (b < 0x80) { k++; continue; }
                if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) { k += 2; continue; }
                if (k + 2 >= s.out_len) break;
                if ((b & 0x0F) == 5 && (b & 0xF0) == 0x90 && s.out[k + 1] == 55) on55_seen = 1;
                if ((b & 0x0F) == 5 && ((b & 0xF0) == 0x80 || ((b & 0xF0) == 0x90 && s.out[k + 2] == 0))
                    && s.out[k + 1] == 67) { off67_seen = 1; if (!on55_seen) order_ok = 1; }
                k += 3;
            }
        }
        if (!on55_seen) FAIL("mono: step 4 never sounded");
        if (!off67_seen) FAIL("mono: an outliving note (67, gate 6) never got its off");
        if (!order_ok) FAIL("mono: the off for 67 must precede the on for 55 in the stream");
        OK("voice mono: a note whose gate outlives the next attack is offed first");
    }
}

static void test_swing(void) {
    ds_state a, b;
    double dt = 64.0 / 44100.0;
    int i, hops_a = 0, hops_b = 0, lo, hi, offs, ons;
    int last_a = -1, last_b = -1;

    one_track(&a, 5);
    one_track(&b, 5);
    /* both tracks: a hit on every step, 8 steps of 1/16 */
    for (i = 1; i < 8; i++)
        ds_voice_add(&a.pat.track[0], i, 60, 100, 1);
    memcpy(&b.pat.track[0].steps, &a.pat.track[0].steps, sizeof a.pat.track[0].steps);
    a.pat.track[0].swing_amt = 100; a.global_swing = 0;     /* straight */
    b.pat.track[0].swing_amt = 100; b.global_swing = 100;   /* full swing */
    ds_play(&a);
    ds_play(&b);
    for (i = 0; i < 20000; i++) {
        ds_tick(&a, dt);
        ds_tick(&b, dt);
        scan(&a, 5, &lo, &hi, &offs, &ons);
        if (ons && !(last_a == 1)) { hops_a += ons; }
        last_a = ons ? 1 : 0;
        scan(&b, 5, &lo, &hi, &offs, &ons);
        if (ons && !(last_b == 1)) { hops_b += ons; }
        last_b = ons ? 1 : 0;
    }
    /* the STEP POSITIONS must differ: swing is a timing change, not a count change */
    {
        int i2, diff = 0;
        ds_state c, d;
        static uint32_t pa[2048], pb[2048];
        int na = 0, nb = 0;
        one_track(&c, 5);
        one_track(&d, 5);
        for (i2 = 1; i2 < 8; i2++) {
            ds_voice_add(&c.pat.track[0], i2, 60, 100, 1);
            ds_voice_add(&d.pat.track[0], i2, 60, 100, 1);
        }
        c.global_swing = 0;  c.pat.track[0].swing_amt = 100;
        d.global_swing = 100; d.pat.track[0].swing_amt = 100;
        ds_play(&c); ds_play(&d);
        for (i2 = 0; i2 < 20000 && (na < 8 || nb < 8); i2++) {
            ds_tick(&c, dt); ds_tick(&d, dt);
            scan(&c, 5, &lo, &hi, &offs, &ons);
            if (ons && na < 2048) pa[na++] = (uint32_t)i2;
            scan(&d, 5, &lo, &hi, &offs, &ons);
            if (ons && nb < 2048) pb[nb++] = (uint32_t)i2;
        }
        for (i2 = 0; i2 < na && i2 < nb; i2++)
            if (pa[i2] != pb[i2]) diff = 1;
        if (na < 8 || nb < 8) FAIL("swing: both runs must still fire 8 notes (a=%d b=%d)", na, nb);
        if (!diff) FAIL("swing: global x per-track = 100 must move the notes in time");
        OK("swing: same note count, different note times");
    }
    /* and the weight scales it: swing_amt 0 must land exactly where straight does */
    {
        ds_state e, f;
        int i3, same = 1;
        one_track(&e, 5); one_track(&f, 5);
        for (i3 = 1; i3 < 8; i3++) {
            ds_voice_add(&e.pat.track[0], i3, 60, 100, 1);
            ds_voice_add(&f.pat.track[0], i3, 60, 100, 1);
        }
        e.global_swing = 0;  e.pat.track[0].swing_amt = 100;
        f.global_swing = 100; f.pat.track[0].swing_amt = 0;   /* weight 0 */
        ds_play(&e); ds_play(&f);
        for (i3 = 0; i3 < 20000; i3++) {
            ds_tick(&e, dt); ds_tick(&f, dt);
            if (e.out_len != f.out_len) { same = 0; break; }
            if (memcmp(e.out, f.out, e.out_len)) { same = 0; break; }
        }
        if (!same) FAIL("swing: per-track weight 0 must be bit-identical to global 0");
        OK("swing: per-track weight 0 cancels the global amount exactly");
    }
    (void)hops_a; (void)hops_b;
}

int main(void) {
    test_route_masks();
    test_mono();
    test_swing();
    printf("TRACK SETTINGS ENGINE TESTS PASSED\n");
    return 0;
}
