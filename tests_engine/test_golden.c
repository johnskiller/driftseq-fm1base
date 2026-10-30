/* SPDX-License-Identifier: MPL-2.0 */
/* test_golden.c — golden MIDI-stream hash of the demo pattern (01 §7 seam contract)
 * Locks the engine's product: demo @ seed 42, 20 s of 64-frame blocks (integer tick
 * count — same loop as the emu) → one FNV-1a chunk hash.
 *
 * Canonical: 1DFFB74BC111252F. It moved from 52CF520E290E322D when a drum
 * channel's program change started sending bank select (CC0 120, CC32 0) before
 * the PC. That is four extra bytes per kit change and it is the ONLY difference:
 * this test also re-runs the stream with the drum channels' bank-select CCs
 * removed and asserts the old 52CF520E290E322D comes back byte for byte, with
 * the same 242 note-ons. If a future change moves the hash without moving the
 * stripped hash too, this fails and says so.
 */
#include "driftseq.h"
#include "demo_pattern.h"
#include <stdio.h>
#include <stdlib.h>

#define GOLDEN_FULL    0x1DFFB74BC111252Full
#define GOLDEN_NOBANK  0x52CF520E290E322Dull

static uint64_t fnv1a(const uint8_t *p, uint32_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001B3ull; }
    return h;
}

/* Is this the start of a drum channel's bank-select CC in a queued message? */
static int is_drum_bank(const uint8_t *m) {
    return (m[0] & 0xF0) == 0xB0 && (m[0] & 0x0F) == 9 && (m[1] == 0 || m[1] == 32);
}

int main(void) {
    ds_state s;
    uint64_t h = 0xCBF29CE484222325ull;      /* the stream as the engine emits it */
    uint64_t hb = 0xCBF29CE484222325ull;     /* the same, minus drum bank select */
    const double dt = 64.0 / 44100.0;
    int note_ons = 0, note_ons_b = 0;
    long bank_msgs = 0;

    ds_load_demo(&s);
    ds_play(&s);
    long iters = (long)(20.0 / dt);          /* integer count — match the emu loop exactly */

    for (long k = 0; k < iters; k++) {
        ds_tick(&s, dt);
        if (!s.out_len)
            continue;

        h = fnv1a(s.out, s.out_len) ^ (h * 0x100000001B3ull + 1);
        for (uint32_t i = 0; i < s.out_len; ) {
            uint8_t b = s.out[i];
            if (b < 0x80) { i++; continue; }
            if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) { i += 2; continue; }
            if ((b & 0xF0) == 0x90 && s.out[i + 2] > 0) note_ons++;
            i += 3;
        }

        /* the same tick with the drum bank CCs deleted */
        {
            uint8_t f[4096];
            uint32_t fl = 0, i;
            for (i = 0; i < s.out_len; ) {
                uint8_t b = s.out[i];
                uint32_t n = ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) ? 2u : 3u;
                if (is_drum_bank(&s.out[i]) && i + 2 < s.out_len) {
                    bank_msgs++;
                } else {
                    uint32_t j;
                    for (j = 0; j < n && i + j < s.out_len; j++)
                        f[fl++] = s.out[i + j];
                }
                i += n;
            }
            if (fl) {
                hb = fnv1a(f, fl) ^ (hb * 0x100000001B3ull + 1);
                for (i = 0; i < fl; ) {
                    uint8_t b = f[i];
                    if (b < 0x80) { i++; continue; }
                    if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) { i += 2; continue; }
                    if ((b & 0xF0) == 0x90 && f[i + 2] > 0) note_ons_b++;
                    i += 3;
                }
            }
        }
    }

    printf("demo 20s: note_ons=%d hash=%016llX\n", note_ons, (unsigned long long)h);
    printf("  minus drum bank select (%ld msgs): note_ons=%d hash=%016llX\n",
           bank_msgs, note_ons_b, (unsigned long long)hb);

    /* sanity: 20 s @120 with the 5 demo tracks ≈ 238 note-ons (drums+bass+lead) */
    if (note_ons < 200) { printf("FAIL: too few note-ons (%d)\n", note_ons); return 1; }
    if (h != GOLDEN_FULL) {
        printf("FAIL: stream hash %016llX, expected %016llX\n",
               (unsigned long long)h, (unsigned long long)GOLDEN_FULL);
        return 1;
    }
    /* The kit/bank change is the only thing that may move the hash. This is the
     * part that catches a real regression hiding behind the new constant. */
    if (hb != GOLDEN_NOBANK) {
        printf("FAIL: hash without drum bank select is %016llX, expected %016llX"
               " — something OTHER than bank select changed the stream\n",
               (unsigned long long)hb, (unsigned long long)GOLDEN_NOBANK);
        return 1;
    }
    if (note_ons != note_ons_b) {
        printf("FAIL: bank select changed the note count (%d vs %d)\n", note_ons, note_ons_b);
        return 1;
    }
    printf("GOLDEN OK\n");
    return 0;
}
