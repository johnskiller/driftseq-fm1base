/* tools/pack_engine_test.c — the round trip must be audible-identical, not just
 * memcmp-identical.
 *
 * pack_test proves the struct survives. This proves the MUSIC survives: run the
 * demo forward 20 s, record every MIDI byte; then pack the pattern, wipe the
 * engine, unpack, run the same 20 s from the same seed, and compare the byte
 * streams. If the save format loses a gate, an `on` bit or a probability, this
 * catches it where the eye would not.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pack.h"
#include "demo_pattern.h"

typedef struct { uint8_t d[8192]; size_t n; } cap;

static void run20s(ds_state *s, cap *c)
{
    int i;
    c->n = 0;
    ds_play(s);
    /* 20 s at the demo's 120 BPM in 5 ms blocks, like the firmware's frame */
    for (i = 0; i < 4000; i++) {
        ds_tick(s, 0.005);
        if (s->out_len) {
            if (c->n + s->out_len < sizeof c->d) {
                memcpy(c->d + c->n, s->out, s->out_len);
                c->n += s->out_len;
            }
            s->out_len = 0;                 /* this test drains the queue itself */
        }
    }
}

int main(void)
{
    static ds_state a, b;
    static uint8_t buf[DS_PACK_PATTERN_MAX + 64];
    static cap ca, cb;
    size_t w, r;
    int fails = 0;

    ds_load_demo(&a);
    run20s(&a, &ca);
    printf("straight run      : %zu bytes of MIDI\n", ca.n);

    /* the demo as packed: how small is a real 5-track pattern? */
    ds_load_demo(&a);
    w = ds_pack_pattern(buf, sizeof buf, &a.pat);
    printf("packed            : %zu B\n", w);

    /* now: wipe, unpack into a fresh engine, run the same thing */
    ds_init(&b, 42);
    r = ds_unpack_pattern(buf, w, &b.pat);
    if (!r) {
        printf("FAIL: unpack refused\n");
        return 1;
    }
    /* the pattern is restored; the non-pattern state (bpm, swing, rng seed) is
     * not part of ds_pattern, so set what ds_load_demo set */
    b.bpm = a.bpm;
    b.global_swing = a.global_swing;
    run20s(&b, &cb);
    printf("after round trip  : %zu bytes of MIDI\n", cb.n);

    if (ca.n != cb.n) {
        printf("FAIL: byte counts differ (%zu vs %zu)\n", ca.n, cb.n);
        fails++;
    } else if (memcmp(ca.d, cb.d, ca.n) != 0) {
        size_t i;
        for (i = 0; i < ca.n; i++)
            if (ca.d[i] != cb.d[i]) { printf("FAIL: first diff at MIDI byte %zu\n", i); break; }
        fails++;
    } else {
        printf("ok  : identical byte stream over 20 s (%zu bytes)\n", ca.n);
    }

    /* an empty pattern must emit no NOTES. It does emit setup traffic on play
     * (program changes and controllers, 32 bytes here), which is correct -- the
     * claim being tested is that no step fires, so look for note-on/off only. */
    ds_init(&b, 7);
    ds_clear_pattern(&b.pat);
    run20s(&b, &cb);
    {
        size_t i, notes = 0;
        for (i = 0; i + 2 < cb.n; i++)
            if ((cb.d[i] & 0xF0u) == 0x90u || (cb.d[i] & 0xF0u) == 0x80u)
                notes++;
        if (!notes)
            printf("ok  : an empty pattern plays no notes (%zu B of setup traffic)\n", cb.n);
        else { printf("FAIL: empty pattern played %zu note messages\n", notes); fails++; }
    }

    printf(fails ? "\nENGINE ROUND TRIP FAILED\n" : "\nENGINE ROUND TRIP OK\n");
    return fails ? 1 : 0;
}
