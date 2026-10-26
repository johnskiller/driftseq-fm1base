/* tools/pack_test.c — round-trip and size proof for the sparse save format.
 *
 * A save format is worthless unless the round trip is byte-exact, so this
 * compares the whole pattern struct with memcmp after pack -> unpack, and does it
 * for the demo, for an empty pattern, and for adversarial tracks (64 steps of 4
 * voices, probability, conditions, euclid, mono, mute, odd gate values).
 *
 * It also prints the sizes, because the number John asked about is "how much does
 * a real pattern cost now", and the answer has to be measured, not argued.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pack.h"
#include "demo_pattern.h"

static int fails;

static void fail(const char *what, int a, int b)
{
    printf("FAIL: %s (%d != %d)\n", what, a, b);
    fails++;
}

/* pack, unpack, compare. Returns bytes used, or -1 on a round-trip failure. */
static long roundtrip(const char *name, const ds_pattern *p)
{
    static uint8_t buf[DS_PACK_PATTERN_MAX + 64];
    static ds_pattern back;
    size_t w, r;

    memset(&back, 0xA5, sizeof back);            /* poison: unpack must fill it */
    w = ds_pack_pattern(buf, sizeof buf, p);
    if (!w) {
        printf("FAIL: %s: pack reported overflow\n", name);
        fails++;
        return -1;
    }
    r = ds_unpack_pattern(buf, w, &back);
    if (!r) {
        printf("FAIL: %s: unpack rejected its own output\n", name);
        fails++;
        return -1;
    }
    if (r != w) {
        fail("bytes consumed", (int)r, (int)w);
    }
    if (memcmp(p, &back, sizeof *p) != 0) {
        /* find the first differing byte so a real failure is debuggable */
        const uint8_t *a = (const uint8_t *)p, *b = (const uint8_t *)&back;
        size_t i;
        for (i = 0; i < sizeof *p; i++)
            if (a[i] != b[i]) {
                printf("FAIL: %s: round trip differs at byte %zu (track %zu, step %zu, field %zu)\n",
                       name, i, i / sizeof(ds_track),
                       (i % sizeof(ds_track)) / sizeof(ds_step),
                       (i % sizeof(ds_step)) % sizeof(ds_note));
                break;
            }
        fails++;
        return -1;
    }
    printf("ok  : %-24s %5zu B  (%.1f%% of the %zu B struct)\n",
           name, w, 100.0 * (double)w / (double)sizeof(ds_pattern), sizeof(ds_pattern));
    return (long)w;
}

int main(void)
{
    static ds_state s;
    ds_pattern *p = &s.pat;
    long demo_b, empty_b, full_b;
    int t, i, vi;

    printf("sizeof(ds_pattern) = %zu B, sizeof(ds_track) = %zu B\n\n",
           sizeof(ds_pattern), sizeof(ds_track));

    /* 1. the demo, which is what a real pattern looks like */
    ds_load_demo(&s);
    demo_b = roundtrip("demo pattern", p);

    /* 2. nothing at all: 16 unused tracks must cost 16 bytes, not 20 KB */
    ds_init(&s, 1);
    empty_b = roundtrip("empty pattern", p);

    /* 3. worst case: every step full, every optional field on */
    ds_init(&s, 2);
    for (t = 0; t < DS_TRACKS; t++) {
        ds_track *tr = &p->track[t];
        tr->loop_len = 64; tr->channel = (uint8_t)t; tr->prog = (uint8_t)(t + 1);
        tr->mute = (uint8_t)(t & 1); tr->swing_amt = (uint8_t)(t * 7 % 101);
        tr->voice_mode = (uint8_t)(t & 1); tr->route = (uint8_t)(t % 3);
        tr->division = (uint8_t)(t % 5);
        for (i = 0; i < DS_STEPS; i++) {
            ds_step *st = &tr->steps[i];
            st->on = (uint8_t)(i & 1);
            st->prob = (uint8_t)(i % 101);
            st->cond = (uint8_t)(i % DS_COND_COUNT);
            st->tie = (uint8_t)(i % 3 == 0);
            st->nvoices = DS_VOICES;
            for (vi = 0; vi < DS_VOICES; vi++) {
                st->v[vi].note = (uint8_t)((i + vi * 7) % 128);
                st->v[vi].vel = (uint8_t)(1 + (i + vi) % 127);
                /* gates: 1 (default, no byte), 0xFF (until next), mid values */
                st->v[vi].gate = (uint8_t)(vi == 0 ? 1 : vi == 1 ? 0xFF : (i + 1));
            }
        }
    }
    full_b = roundtrip("worst case (all full)", p);

    /* 4. a euclid track with no steps: meta only, so the 64-step array must not
     *    be written at all */
    ds_init(&s, 3);
    p->track[5].eu_on = 1; p->track[5].eu_k = 5; p->track[5].eu_n = 16; p->track[5].eu_rot = 2;
    roundtrip("one euclid meta track", p);

    /* 5. a one-note track: the number that matters for a bass line */
    ds_init(&s, 4);
    ds_voice_add(&p->track[3], 0, 36, 100, 2);
    roundtrip("one note on one track", p);

    /* 5b. a tie step and NOTHING else: no on, no voice. This is the state that
     *     a naive "does this step have content?" test throws away, which is
     *     exactly how the format lost ties on the first pass. */
    ds_init(&s, 6);
    p->track[7].loop_len = 8;
    ds_voice_add(&p->track[7], 0, 48, 90, 1);
    p->track[7].steps[0].tie = 0;
    p->track[7].steps[1].tie = 1;
    p->track[7].steps[2].tie = 1;
    p->track[7].steps[2].on = 0;
    p->track[7].steps[2].nvoices = 0;
    roundtrip("a tie with no on and no voice", p);

    /* 6. corruption must be REFUSED, not clamped */
    {
        static uint8_t buf[DS_PACK_PATTERN_MAX + 64];
        static ds_pattern back;
        size_t w, len, n;
        ds_init(&s, 5);
        ds_load_demo(&s);
        w = ds_pack_pattern(buf, sizeof buf, p);

        /* Build a record whose layout is known instead of assuming offsets into a
         * demo record: a non-euclid track makes the fields land at fixed
         * positions, so "loop_len = 0" can be written where loop_len really is.
         *   [0] flags [1] division [2] loop_len [3] loop_start ... */
        ds_init(&s, 5);
        ds_voice_add(&p->track[0], 0, 60, 100, 1);
        len = ds_pack_pattern(buf, sizeof buf, p);
        if (!len || len < 5) {
            printf("FAIL: could not build a test record\n");
            fails++;
        } else {
            buf[2] = 0;                        /* loop_len = 0 */
            if (ds_unpack_pattern(buf, len, &back) == 0)
                printf("ok  : corrupt record (loop_len=0) was refused\n");
            else { printf("FAIL: corrupt record (loop_len=0) was accepted\n"); fails++; }

            /* an out-of-range step index must be refused too */
            ds_init(&s, 5);
            ds_voice_add(&p->track[0], 63, 60, 100, 1);
            len = ds_pack_pattern(buf, sizeof buf, p);
            /* the run's gap byte is the 12th byte of a non-euclid record:
             * flags + 8 meta + nsteps = 10, then gap, then hdr */
            if (len > 10) {
                buf[10] = 200;                 /* gap 200 -> step 200 */
                if (ds_unpack_pattern(buf, len, &back) == 0)
                    printf("ok  : corrupt record (step index 200) was refused\n");
                else { printf("FAIL: step index 200 was accepted\n"); fails++; }
            }

            /* a truncated buffer must be refused */
            if (ds_unpack_pattern(buf, 3, &back) == 0)
                printf("ok  : truncated record was refused\n");
            else { printf("FAIL: truncated record was accepted\n"); fails++; }

            /* and a buffer that is purely zeroes is 16 empty tracks, not an error */
            memset(buf, 0, DS_TRACKS);
            ds_init(&s, 6);
            n = ds_unpack_pattern(buf, DS_TRACKS, &back);
            if (n == DS_TRACKS && memcmp(&back, p, sizeof back) == 0)
                printf("ok  : 16 zero bytes unpack to an empty pattern\n");
            else { printf("FAIL: 16 zero bytes gave %zu\n", n); fails++; }
        }
        (void)w;
    }

    printf("\n--- the answer ---\n");
    printf("today, unconditionally   : %6zu B per pattern (16 x %zu B)\n", sizeof(ds_pattern), sizeof(ds_track));
    printf("empty pattern, per track : %6ld B total (%ld B per track)\n", empty_b, empty_b / DS_TRACKS);
    printf("the demo pattern         : %6ld B  -> %.1f%% of today\n",
           demo_b, 100.0 * (double)demo_b / (double)sizeof(ds_pattern));
    printf("worst case, all full     : %6ld B  -> %.1f%% of today\n",
           full_b, 100.0 * (double)full_b / (double)sizeof(ds_pattern));
    printf("hard ceiling per track   : %6u B\n", DS_PACK_TRACK_MAX);

    printf(fails ? "\nPACK FAILED (%d)\n" : "\nPACK OK\n", fails);
    return fails ? 1 : 0;
}
