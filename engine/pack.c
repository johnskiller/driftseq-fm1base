/* SPDX-License-Identifier: MPL-2.0 */
/* pack.c — sparse per-track save format (see pack.h for the wire layout).
 *
 * Written as a byte stream on purpose: no struct is memcpy'd into the buffer and
 * no pointer is cast into it, so the format does not depend on the compiler's
 * padding or the MCU's endianness. A pattern saved by today's build must load in
 * next year's build -- that is the whole point of a save format.
 *
 * Two rules the code keeps to, both because a corrupt partition must not be able
 * to do worse than lose one track:
 *
 *   1. Reads validate, never clamp. A loop_len of 0 or a step index past 63
 *      makes the record rejected, not "fixed up" into something plausible.
 *   2. A track either unpacks completely or leaves itself at defaults. There is
 *      no half-track state a reader could mistake for real content.
 */
#include "pack.h"

/* fm1-base builds the app as ONE unity translation unit and its libc
 * (platform/bsp/chip/libc.c) declares memcmp/memset with `unsigned` lengths --
 * there is no <string.h> in the target's include paths at all, even though clang
 * compiles hosted here (no -ffreestanding, so __STDC_HOSTED__ is 1 and cannot be
 * used to detect it). The host builds in tools/ *do* have <string.h> and a
 * different prototype, so ask for the header only when it exists. */
#if defined(__has_include)
#  if __has_include(<string.h>)
#    include <string.h>
#    define DS_HAVE_STRING_H 1
#  endif
#endif
#ifndef DS_HAVE_STRING_H
int memcmp(const void *a, const void *b, unsigned n);
void *memset(void *d, int c, unsigned n);
#endif

/* flags, first byte of every track record */
#define TF_USED      0x01u
#define TF_METASONLY 0x02u
#define TF_EUCLID    0x04u

/* step_run header bits */
#define SH_NVOICES_M 0x07u
#define SH_ON        0x08u
#define SH_PROB      0x10u
#define SH_COND      0x20u
#define SH_TIE       0x40u

/* voice note byte: bit7 says a gate byte follows, bits0-6 are the note. MIDI
 * notes are 0..127, so the high bit is free for exactly this. */
#define VH_GATE      0x80u
#define VH_NOTE_M    0x7Fu

/* ds_track has no padding (every member is uint8_t or an array of them), which
 * is what lets the default comparison below be a memcmp. If that ever stops
 * being true this fails the build instead of silently comparing garbage.
 * 64 steps x 17 B + 14 meta bytes = 1,102 (rev.28 added live-only solo; the
 * packer still writes 13 — solo is deliberately not serialized). */
typedef char ds_pack_track_size_check[
    (sizeof(ds_track) == DS_STEPS * (DS_VOICES * 3u + 5u) + 14u) ? 1 : -1];

/* a step is content if it holds a note OR its `on` bit is set. `on` alone is the
 * silent-keep state the UI toggles; dropping it on save would resurrect notes
 * the user had muted. */
static int step_has_content(const ds_step *st)
{
    uint8_t vi;
    if (st->on || st->tie)      /* tie is content with neither on nor a voice */
        return 1;
    for (vi = 0; vi < DS_VOICES; vi++)
        if (st->v[vi].note != 0xFF)
            return 1;
    return 0;
}

/* Compare against the engine's own idea of "empty" rather than a hand-copied
 * list of constants: ds_clear_track has eight of them and any one drifting would
 * quietly cost 1,292 bytes per unused track. */
static int track_is_default(const ds_track *t)
{
    ds_track blank;
    ds_clear_track(&blank);
    return memcmp(t, &blank, sizeof blank) == 0;
}

/* highest used voice index + 1; a hole inside the array cannot make nvoices
 * overstate what follows */
static uint8_t used_voices(const ds_step *st)
{
    uint8_t vi, n = 0;
    for (vi = 0; vi < DS_VOICES; vi++)
        if (st->v[vi].note != 0xFF)
            n = (uint8_t)(vi + 1);
    return n;
}

/* bytes a run will occupy -- used to check capacity BEFORE writing, so an
 * overflow leaves nothing behind */
static size_t run_size(const ds_step *st)
{
    uint8_t vi, nv = used_voices(st);
    size_t n = 2;                                     /* gap + hdr */
    if (st->prob != 100)
        n++;
    if (st->cond != DS_C_NONE)
        n++;
    for (vi = 0; vi < nv; vi++)
        n += (st->v[vi].gate != 1) ? 3u : 2u;
    return n;
}

size_t ds_pack_track(uint8_t *out, size_t cap, const ds_track *t)
{
    size_t n = 0, need = 0;
    int i, prev = -1;
    uint8_t vi, nsteps = 0, flags;

    if (!out || !t)
        return 0;

    if (track_is_default(t)) {
        if (cap < 1)
            return 0;
        out[0] = 0;                                   /* not USED */
        return 1;
    }

    for (i = 0; i < DS_STEPS; i++)
        if (step_has_content(&t->steps[i]))
            nsteps++;

    flags = TF_USED;
    if (!nsteps)
        flags |= TF_METASONLY;
    if (t->eu_on)
        flags |= TF_EUCLID;

    /* header: flags + optional euclid pair + 8 meta + nsteps */
    need = 1u + (t->eu_on ? 2u : 0u) + 8u + 1u;
    for (i = 0; i < DS_STEPS; i++) {
        const ds_step *st = &t->steps[i];
        if (!step_has_content(st))
            continue;
        if (used_voices(st) == 0 && st->nvoices != 0)
            return 0;                                 /* inconsistent input */
        need += run_size(st);
    }
    if (need > cap)
        return 0;

    out[n++] = flags;
    if (t->eu_on) {
        /* k is 1..64 (7 bits) and rot is 0..3 (2 bits): 9 bits, so two bytes.
         * Packing them into one would cap n at 32, and n=64 is legal. */
        out[n++] = (uint8_t)(t->eu_k & 0x7Fu);
        out[n++] = (uint8_t)((t->eu_n & 0x7Fu) | ((t->eu_rot & 0x03u) << 6));
    }
    out[n++] = t->division;
    out[n++] = t->loop_len;
    out[n++] = t->loop_start;
    out[n++] = t->channel;
    out[n++] = t->route;
    out[n++] = t->prog;
    out[n++] = (uint8_t)((t->mute ? 1u : 0u) | ((t->swing_amt & 0x7Fu) << 1));
    out[n++] = t->voice_mode;
    out[n++] = nsteps;

    for (i = 0; i < DS_STEPS; i++) {
        const ds_step *st = &t->steps[i];
        uint8_t hdr, nv;
        if (!step_has_content(st))
            continue;
        nv = used_voices(st);
        /* nvoices field: 3 bits, 0..4 (0 = a bare gate: on or tie with no voice).
         * The first version stored nv-1 in 2 bits, which could not express 0 and
         * made a bare gate unpack one phantom voice. */
        hdr = (uint8_t)(nv ? nv : 0u);
        if (st->on)
            hdr |= SH_ON;
        if (st->prob != 100)
            hdr |= SH_PROB;
        if (st->cond != DS_C_NONE)
            hdr |= SH_COND;
        if (st->tie)
            hdr |= SH_TIE;

        out[n++] = (uint8_t)(i - prev - 1);           /* gap since last run */
        out[n++] = hdr;
        if (st->prob != 100)
            out[n++] = st->prob;
        if (st->cond != DS_C_NONE)
            out[n++] = st->cond;
        for (vi = 0; vi < nv; vi++) {
            uint8_t gate_opt = (uint8_t)(st->v[vi].gate != 1);
            out[n++] = (uint8_t)((st->v[vi].note & VH_NOTE_M) | (gate_opt ? VH_GATE : 0u));
            out[n++] = st->v[vi].vel;
            if (gate_opt)
                out[n++] = st->v[vi].gate;
        }
        prev = i;
    }
    return n;
}

size_t ds_unpack_track(const uint8_t *in, size_t len, ds_track *t)
{
    size_t n = 0;
    uint8_t flags, nsteps, vi, hdr, nv;
    int step = -1, r;

    if (!in || !t || len < 1)
        return 0;

    ds_clear_track(t);
    flags = in[n++];
    if (!(flags & TF_USED))
        return 1;                                     /* 1 byte: track is empty */

    if (flags & TF_EUCLID) {
        if (n + 2 > len) return 0;
        t->eu_on = 1;
        t->eu_k = (uint8_t)(in[n] & 0x7Fu);
        t->eu_n = (uint8_t)(in[n + 1] & 0x3Fu);
        t->eu_rot = (uint8_t)((in[n + 1] >> 6) & 0x03u);
        n += 2;
    }
    if (n + 9 > len) return 0;
    t->division   = in[n++];
    t->loop_len   = in[n++];
    t->loop_start = in[n++];
    t->channel    = in[n++];
    t->route      = in[n++];
    t->prog       = in[n++];
    { uint8_t b = in[n++]; t->mute = (uint8_t)(b & 1u); t->swing_amt = (uint8_t)((b >> 1) & 0x7Fu); }
    t->voice_mode = in[n++];
    nsteps = in[n++];

    /* validate before trusting any of it */
    if (t->loop_len < 1 || t->loop_len > DS_STEPS) return 0;
    if (t->loop_start >= DS_STEPS) return 0;
    if (t->division >= 5 || t->route > DS_ROUTE_BOTH) return 0;
    if (t->channel > 15 || t->voice_mode > DS_VOICE_MONO) return 0;
    if (t->swing_amt > 100) return 0;
    if (t->eu_on && (t->eu_n < 1 || t->eu_n > DS_STEPS || t->eu_k > t->eu_n)) return 0;

    for (r = 0; r < nsteps; r++) {
        ds_step *st;
        uint8_t gap;
        if (n + 2 > len) return 0;
        gap = in[n++];
        step = step + 1 + (int)gap;
        if (step >= DS_STEPS) return 0;
        hdr = in[n++];
        nv = (uint8_t)(hdr & SH_NVOICES_M);
        st = &t->steps[step];
        st->on = (hdr & SH_ON) ? 1 : 0;
        st->tie = (hdr & SH_TIE) ? 1 : 0;
        if (hdr & SH_PROB) { if (n >= len) return 0; st->prob = in[n++]; }
        if (hdr & SH_COND) { if (n >= len) return 0; st->cond = in[n++]; }
        if (st->prob > 100 || st->cond >= DS_COND_COUNT) return 0;
        if (nv > DS_VOICES) return 0;                 /* 3 bits can hold 5..7 */
        for (vi = 0; vi < nv; vi++) {
            uint8_t nb;
            if (n + 2 > len) return 0;
            nb = in[n++];
            st->v[vi].note = (uint8_t)(nb & VH_NOTE_M);
            st->v[vi].vel = in[n++];
            if (nb & VH_GATE) {
                if (n >= len) return 0;
                st->v[vi].gate = in[n++];
            } else {
                st->v[vi].gate = 1;
            }
        }
        st->nvoices = nv;
    }
    return n;
}

size_t ds_pack_pattern(uint8_t *out, size_t cap, const ds_pattern *p)
{
    size_t n = 0;
    int i;
    for (i = 0; i < DS_TRACKS; i++) {
        size_t w = ds_pack_track(out + n, cap - n, &p->track[i]);
        if (!w)
            return 0;
        n += w;
    }
    return n;
}

size_t ds_unpack_pattern(const uint8_t *in, size_t len, ds_pattern *p)
{
    size_t n = 0;
    int i;
    for (i = 0; i < DS_TRACKS; i++) {
        size_t w = ds_unpack_track(in + n, len - n, &p->track[i]);
        if (!w)
            return 0;                    /* one bad track fails the pattern: half
                                          * a pattern is worse than a refusal,
                                          * the user still has what they were
                                          * editing */
        n += w;
    }
    return n;
}
