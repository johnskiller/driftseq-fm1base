/* SPDX-License-Identifier: MPL-2.0 */
/* driftseq.c — deterministic sequencer engine (spec 01-core-model)
 *
 * Contract: ds_tick(state, dt) emits this tick's MIDI bytes into state->out.
 * Same state + same dt sequence -> same bytes (seeded RNG, all scheduler state
 * in ds_state — no hidden globals). The product IS the MIDI byte stream (01 §7).
 *
 * Core semantics:
 *  - Each track's loop window [loop_start, +loop_len) IS the loop: the playhead
 *    wraps inside it (SQ-64 RANGE). Different window lengths + divisions => the
 *    tracks drift apart and re-align at LCM — that is the product.
 *  - pos16[] counts CELLS of the track's division (16th grid slots, re-timed by
 *    division). Play launches every track on its own loop_start (aligned at t=0,
 *    drifting after).
 *  - Live window morph (K3/K4) is just a param read per tick: playhead outside
 *    the new window snaps to the new start, notes are never hard-cut.
 */
#include "driftseq.h"
/* The firmware unity root includes src/libc.c (a freestanding libc with no
 * header) BEFORE this file, and pi32v2 has no libc headers at all. Every other
 * build (host tests, wasm, hostcheck) has a real string.h. */
#ifdef DRIFTSEQ_FW
void *memset(void *d, int c, unsigned n);
#else
#include <string.h>
#endif

/* ---------- rng (xorshift64) ---------- */
void ds_rng_seed(ds_rng *r, uint64_t seed) { r->state = seed ? seed : 0x9E3779B97F4A7C15ull; }
uint32_t ds_rng_next(ds_rng *r) {
    uint64_t x = r->state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    r->state = x;
    return (uint32_t)(x >> 16);
}

/* ---------- init ---------- */
void ds_clear_track(ds_track *t) {
    ds_step *st;
    uint32_t i;
    memset(t, 0, sizeof *t);
    for (i = 0; i < DS_STEPS; i++) {
        st = &t->steps[i];
        memset(st->v, 0xFF, sizeof st->v);   /* note 0xFF = empty */
        st->prob = 100;
        st->cond = DS_C_NONE;
    }
    t->loop_len = 16;
    t->loop_start = 0;
    t->division = DS_DIV_16;
    t->route = DS_ROUTE_BOTH;
    t->prog = 0;
    t->swing_amt = 100;
    t->voice_mode = DS_VOICE_POLY;
    t->eu_n = 16; t->eu_k = 4;
}
void ds_clear_pattern(ds_pattern *p) {
    int i;
    for (i = 0; i < DS_TRACKS; i++) ds_clear_track(&p->track[i]);
}
void ds_init(ds_state *s, uint64_t seed) {
    int i;
    memset(s, 0, sizeof *s);
    ds_clear_pattern(&s->pat);
#if DS_PATTERN_BANK_IN_STATE
    for (i = 0; i < DS_PATTERNS; i++) ds_clear_pattern(&s->flash[i]);
#else
    (void)i;
#endif
    s->bpm = 120.0;
    s->global_swing = 0;
    {   int k;
        for (k = 0; k < DS_TRACKS; k++)
            s->mono_note[k] = 0xFF;          /* MONO starts holding nothing (memset's 0 = note 0) */
    }
    ds_route_sync(s);                        /* memset left every route at USB-only */
    ds_rng_seed(&s->rng, seed);
    ds_jam_reset(s);                         /* the jam ring starts empty and armed */
}

/* ---------- transport ---------- */
void ds_gates_reset(ds_state *s);
void ds_play(ds_state *s) {
    int i;
    s->playing = 1;
    s->tick_acc = 0.0;
    ds_gates_reset(s);
    ds_jam_close_held(s);                    /* a hold across PLAY finalizes at the old counter */
    /* the ring is NOT cleared here: "jam over the demo, then PLAY to hear the
     * two together, then SAVE" is the main line. The player's take survives;
     * only its phase re-anchors so ring step 0 = the next bar line. */
    for (i = 0; i < DS_TRACKS; i++) {
        s->sent_prog[i] = 0xFF;
        s->mono_note[i] = 0xFF;                  /* MONO holds nothing after a rewind */
        s->pos16[i] = s->pat.track[i].loop_start;  /* each track launches on its own start */
        s->frac16[i] = 0.0;
        s->pending[i] = 1;                          /* start step sounds at t=0 */
        s->chain_cycle[i] = 0;
    }
}
void ds_stop(ds_state *s) {
    int i;
    s->playing = 0;
    s->gates_n = 0;                                  /* pending gate-offs die with the transport */
    for (i = 0; i < DS_TRACKS; i++)
        s->mono_note[i] = 0xFF;                      /* nothing is held after an all-notes-off */
    s->out_len = 0;                                  /* stop's bytes are the whole message */
    for (i = 0; i < DS_TRACKS; i++) {               /* all notes off, part of the stream */
        s->out[s->out_len++] = 0xB0 | (s->pat.track[i].channel & 0x0F);
        s->out[s->out_len++] = 123;
        s->out[s->out_len++] = 0;
    }
    s->live_n = 0;                                   /* held step-entry notes die with the transport */
}

/* ---- live keyboard ---- */
int ds_live_on(ds_state *s, uint8_t trk, uint8_t note, uint8_t vel) {
    int i;
    if (s->out_len + 3 > sizeof s->out) return -1;
    s->out[s->out_len++] = 0x90 | (s->pat.track[trk].channel & 0x0F);
    s->out[s->out_len++] = note;
    s->out[s->out_len++] = vel;
    for (i = 0; i < s->live_n; i++)
        if (s->live[i].trk == trk && s->live[i].note == note) { s->live[i].t = 0; return 0; }
    if (s->live_n >= 16) return -2;                  /* pool full: oldest is dropped by caller policy */
    s->live[s->live_n].trk = trk; s->live[s->live_n].note = note; s->live[s->live_n].t = 0;
    s->live[s->live_n].ch = (uint8_t)(s->pat.track[trk].channel & 0x0F);   /* where the on went */
    s->live_n++;
    return 0;
}
int ds_live_off(ds_state *s, uint8_t trk, uint8_t note) {
    int i, j, held = 0;
    for (i = 0; i < s->live_n; i++)
        if (s->live[i].trk == trk && s->live[i].note == note) { held = 1; break; }
    if (!held) return 0;                             /* stale: auto gate already sent the note-off */
    if (s->out_len + 3 > sizeof s->out) return -1;
    s->out[s->out_len++] = 0x80 | s->live[i].ch;     /* the channel the note-on used */
    s->out[s->out_len++] = note;
    s->out[s->out_len++] = 0;
    for (i = 0; i < s->live_n; i++) {
        if (s->live[i].trk == trk && s->live[i].note == note) {
            for (j = i; j + 1 < s->live_n; j++) s->live[j] = s->live[j + 1];
            s->live_n--;
            break;
        }
    }
    return 1;
}
void ds_set_bpm(ds_state *s, double bpm) { if (bpm >= 20.0 && bpm <= 300.0) s->bpm = bpm; }

/* scheduled note-offs for SEQUENCED notes. Without this the `gate` field is
 * dead data: land() only ever emitted note-on, so a played pattern hung every
 * note until the next all-notes-off. Gate unit = steps of the track's own
 * division (01 §2); 0xFF = hold until the next event in the window or the
 * loop end, resolved at emit time so live edits take effect immediately. */
typedef struct { uint8_t trk, note, ch; double t; } ds_gate_ev;   /* (the pool lives in ds_state) */
static double cell_seconds(double bpm, int div);   /* defined below */
static void jam_advance(ds_state *s, double dt);   /* jam ring clock (defined below) */

/* routing: two destinations, per channel. USB and BLE share one wire (the
 * platform's TO_USB|TO_BLE is one send), GM is the local synth. DS_ROUTE_* is
 * per track, but the wire and GM are per CHANNEL, so two tracks on one channel
 * OR their bits. Recomputed rather than stored per track so a fresh unpacked
 * pattern needs no fixup pass; ds_tick calls it, and the UI calls it after a
 * ROUTE edit so the very next drain already respects the change. */
void ds_route_sync(ds_state *s)
{
    int i;
    s->usb_mask = 0;
    s->ble_mask = 0;
    for (i = 0; i < DS_TRACKS; i++) {
        const ds_track *t = &s->pat.track[i];
        uint16_t bit = (uint16_t)(1u << (t->channel & 0x0F));
        if (t->route != DS_ROUTE_BLE)
            s->usb_mask |= bit;
        if (t->route != DS_ROUTE_USB)
            s->ble_mask |= bit;
    }
}

void ds_gates_reset(ds_state *s) { s->gates_n = 0; }
static void gate_schedule(ds_state *s, const ds_track *t, uint8_t trk,
                          const ds_note *n, double steps)
{
    double need;
    if (s->gates_n >= DS_GATES)
        return;
    if (steps <= 0.0)
        steps = 1.0;
    need = steps * cell_seconds(s->bpm, t->division);
    s->gates[s->gates_n].trk = trk;
    s->gates[s->gates_n].note = n->note;
    /* the channel is captured HERE, not read back at fire time: the Track
     * Settings page can change a track's channel while its note is sounding,
     * and an off sent to the new channel would leave the note stuck on the old
     * one. The off must go where the on went. */
    s->gates[s->gates_n].ch = (uint8_t)(t->channel & 0x0F);
    s->gates[s->gates_n].t = need;
    s->gates_n++;
}
/* advance the pool, emitting 0x80 for every expired entry */
static int gate_advance(ds_state *s, double dt)
{
    int i, fired = 0;
    for (i = (int)s->gates_n - 1; i >= 0; i--) {
        s->gates[i].t -= dt;
        if (s->gates[i].t > 0.0)
            continue;
        if (s->out_len + 3 > sizeof s->out) {
            s->gates[i].t = 0.0;                /* keep it: retry next tick */
            continue;
        }
        s->out[s->out_len++] = 0x80 | s->gates[i].ch;
        s->out[s->out_len++] = s->gates[i].note & 0x7F;
        s->out[s->out_len++] = 0;
        if (s->mono_note[s->gates[i].trk] == s->gates[i].note)
            s->mono_note[s->gates[i].trk] = 0xFF;   /* MONO: the held note just died */
        s->gates[i] = s->gates[s->gates_n - 1];
        s->gates_n--;
        fired = 1;
    }
    return fired;
}
int ds_voice_add(ds_track *t, uint32_t step, uint8_t note, uint8_t vel, uint8_t gate) {
    ds_step *st = &t->steps[step & 63];
    int i;
    if (st->nvoices >= DS_VOICES) return -1;
    for (i = 0; i < st->nvoices; i++)
        if (st->v[i].note == note) return -2;
    st->v[st->nvoices].note = note;
    st->v[st->nvoices].vel = vel;
    st->v[st->nvoices].gate = gate ? gate : 1;
    st->nvoices++;
    st->on = 1;                       /* adding a voice lights the step (single-click revives) */
    return 0;
}
int ds_voice_remove(ds_track *t, uint32_t step, uint8_t note) {
    ds_step *st = &t->steps[step & 63];
    int i, j;
    for (i = 0; i < st->nvoices; i++) {
        if (st->v[i].note == note) {
            for (j = i; j < st->nvoices - 1; j++) st->v[j] = st->v[j + 1];
            st->v[st->nvoices - 1].note = 0xFF;
            st->nvoices--;
            if (!st->nvoices) st->on = 0;
            return 0;
        }
    }
    return -1;
}
uint8_t ds_step_active(const ds_track *t, uint32_t step) {
    uint32_t i = step & 63;
    uint32_t rel = (i + 64 - t->loop_start) & 63;
    if (rel >= t->loop_len) return 0;
    return t->steps[i].nvoices > 0 && t->steps[i].on;
}

/* Euclid: k pulses over n slots — real Bjorklund (Toussaint 2005, 03 §4.2).
 * Bucket algorithm: pair A-buckets (1s) with B-buckets (0s) while more than one
 * B-bucket remains; leftovers of the longer list become the new B list; output =
 * A buckets then B buckets. Canonical phase verified against the paper:
 * E(8,3)=tresillo x..x..x., E(5,12)=Venda x..x.x..x.x., E(5,16)=bossa necklace,
 * E(7,16)=samba necklace (host/test_euclid.c). Scratch = 2×64 buckets × 64B = 8 KiB
 * stack — fine on host; firmware port: reuse a shared scratch buffer instead.
 * rot shifts pulses later: on(i) = pat[(rel - rot) mod n], rel from loop_start.
 * Fill writes ONLY en bits (voices/vel/gate/prob untouched — re-fill idempotent). */
static void bjorklund(int n, int k, uint8_t *out) {
    /* Flat, not [64][64], and still static. The buckets' item counts always sum
     * to n <= 64, so every bucket list together occupies at most 64 bytes and a
     * flat array plus per-bucket offsets holds exactly the same thing. The 2-D
     * version was 64*64 for each of A, B, nA and nB = 16 KiB of .bss, which the
     * FM-1's 96 KiB of RAM cannot spare next to the BLE stack (it overflowed by
     * 20 KB). Kept static rather than moved to the stack because this function
     * is reached from the fill path and the device's stack is not the place for
     * 1.3 KiB of scratch. Same algorithm, same output -- the golden stream is
     * the check. */
    static uint8_t A[64], B[64], nA[64], nB[64];
    static int aoff[65], boff[65], naoff[65], nboff[65], alen[64], blen[64], nal[64], nbl[64];
    int na = k, nb = n - k, i, j;
    if (k <= 0) { for (i = 0; i < n; i++) out[i] = 0; return; }
    if (k >= n) { for (i = 0; i < n; i++) out[i] = 1; return; }
    for (i = 0; i < na; i++) { aoff[i] = i; A[i] = 1; alen[i] = 1; }
    for (i = 0; i < nb; i++) { boff[i] = i; B[i] = 0; blen[i] = 1; }
    while (nb > 1) {
        int m = na < nb ? na : nb, nna = m, nnb = 0, w = 0;
        for (i = 0; i < m; i++) {                    /* pair: A[i] then B[i] */
            naoff[i] = w;
            for (j = 0; j < alen[i]; j++) nA[w + j] = A[aoff[i] + j];
            for (j = 0; j < blen[i]; j++) nA[w + alen[i] + j] = B[boff[i] + j];
            w += alen[i] + blen[i];
            nal[i] = alen[i] + blen[i];
        }
        for (i = m; i < na; i++) {                   /* leftover A waits in the B list */
            nboff[nnb] = w;
            for (j = 0; j < alen[i]; j++) nB[w + j] = A[aoff[i] + j];
            w += alen[i];
            nbl[nnb++] = alen[i];
        }
        for (i = m; i < nb; i++) {                   /* leftover B waits too */
            nboff[nnb] = w;
            for (j = 0; j < blen[i]; j++) nB[w + j] = B[boff[i] + j];
            w += blen[i];
            nbl[nnb++] = blen[i];
        }
        for (i = 0; i < nna; i++) { aoff[i] = naoff[i]; for (j = 0; j < nal[i]; j++) A[naoff[i] + j] = nA[naoff[i] + j]; alen[i] = nal[i]; }
        for (i = 0; i < nnb; i++) { boff[i] = nboff[i]; for (j = 0; j < nbl[i]; j++) B[nboff[i] + j] = nB[nboff[i] + j]; blen[i] = nbl[i]; }
        na = nna; nb = nnb;
    }
    {   int w = 0;                                   /* paper: output = A buckets ++ B buckets */
        for (i = 0; i < na; i++) for (j = 0; j < alen[i]; j++) out[w++] = A[aoff[i] + j];
        for (i = 0; i < nb; i++) for (j = 0; j < blen[i]; j++) out[w++] = B[boff[i] + j];
    }
}

void ds_bjorklund(int n, int k, uint8_t *out) { bjorklund(n, k, out); }
void ds_euclid_fill(ds_track *t) {
    uint8_t pat[64];
    int i, n = t->eu_n ? t->eu_n : 16, k = t->eu_k;
    if (k > n) k = n;
    if (k < 0) k = 0;
    bjorklund(n, k, pat);
    for (i = 0; i < DS_STEPS; i++) {
        uint32_t rel = (i + 64 - t->loop_start) & 63;
        uint32_t rot = t->eu_rot % (uint32_t)n;
        uint32_t idx = rel >= rot ? rel - rot : rel + (uint32_t)n - rot;
        t->steps[i].on = rel < (uint32_t)t->loop_len ? pat[idx] : 0;
    }
}

/* ---------- conditions (01 §4) ---------- */
static int cond_ok(const ds_step *st, const ds_state *s, int trk) {
    switch (st->cond) {
    case DS_C_EVERY2: return ((s->chain_cycle[trk] + 1) % 2) == 0;  /* pass N reads N-1 done */
    case DS_C_EVERY3: return ((s->chain_cycle[trk] + 1) % 3) == 0;
    case DS_C_EVERY4: return ((s->chain_cycle[trk] + 1) % 4) == 0;
    case DS_C_EVERY5: return ((s->chain_cycle[trk] + 1) % 5) == 0;
    case DS_C_EVERY6: return ((s->chain_cycle[trk] + 1) % 6) == 0;
    case DS_C_EVERY7: return ((s->chain_cycle[trk] + 1) % 7) == 0;
    case DS_C_FIRST:  return s->chain_cycle[trk] == 0;
    case DS_C_LAST4:  return 1;      /* v1: chain remainder per track is TODO */
    case DS_C_FILL:   return s->fill_flag != 0;
    case DS_C_FLAM:   return 1;      /* v1: previous-step result tracking TODO */
    default:          return 1;
    }
}

/* ---------- the tick ---------- */
static double cell_seconds(double bpm, int div) {
    double sixteenth = 60.0 / bpm / 4.0;
    switch (div) {
    case DS_DIV_8:   return sixteenth * 2.0;
    case DS_DIV_8T:  return sixteenth * 4.0 / 3.0;
    case DS_DIV_16:  return sixteenth;
    case DS_DIV_16T: return sixteenth * 2.0 / 3.0;
    case DS_DIV_32:  return sixteenth * 0.5;
    default:         return sixteenth;
    }
}

static void emit_note(ds_state *s, const ds_track *t, const ds_note *n) {
    if (s->out_len + 3 > sizeof s->out) return;
    s->out[s->out_len++] = 0x90 | (t->channel & 0x0F);
    s->out[s->out_len++] = n->note & 0x7F;
    s->out[s->out_len++] = n->vel & 0x7F;
}
/* Does this track have anything to play? Only steps inside its own window
 * count: notes parked outside loop_len never sound, so a track that is all
 * faded-out edits is as silent as an empty one and must not hold a channel's
 * program hostage. */
int ds_track_has_notes(const ds_state *s, int trk)
{
    const ds_track *t = &s->pat.track[trk];
    uint32_t i, len = t->loop_len > DS_STEPS ? DS_STEPS : t->loop_len;
    for (i = 0; i < len; i++)
        if (t->steps[i].nvoices)
            return 1;
    return 0;
}

/* Which track decides each channel's program, 0xFF = nobody. The receiver has
 * ONE program per channel, so a shared channel needs a single owner: the
 * lowest-numbered track that is audible and has notes. Lowest-numbered (not
 * last-writer-wins) so adding a track never silently steals the sound of a
 * busy one, and so the answer is stable across ticks and predictable on the
 * TRACK SET page. Derived from the pattern, never cached, which is what makes
 * a pattern switch free: the owner is a property of the pattern, so the next
 * tick after a switch answers for the new one. Built once per tick with an
 * early exit per channel (a channel stops looking the moment it has an owner),
 * so the scan costs at most one pass over the pattern's steps. */
static void build_owners(const ds_state *s, uint8_t *owner, int any_solo)
{
    int i;
    for (i = 0; i < 16; i++) owner[i] = 0xFF;
    for (i = 0; i < DS_TRACKS; i++) {
        const ds_track *t = &s->pat.track[i];
        uint8_t ch = (uint8_t)(t->channel & 0x0F);
        if (t->mute) continue;
        if (any_solo && !t->solo) continue;
        if (owner[ch] != 0xFF) continue;
        if (ds_track_has_notes(s, i)) owner[ch] = (uint8_t)i;
    }
}

static void emit_pc(ds_state *s, const ds_track *t) {
    if (s->out_len + 2 > sizeof s->out) return;
    s->out[s->out_len++] = 0xC0 | (t->channel & 0x0F);
    s->out[s->out_len++] = t->prog & 0x7F;
}

/* The program the receiver should hold on this track's channel, now.
 *
 * The tick path sends it while the transport runs (owner + sent_prog), but a
 * stopped transport returns before that block - so on TRACK SET the PROG row
 * changed the track and the synth never heard: the keys auditioned the OLD
 * voice and the row read as broken (John 2026-10-30: "track set的program change
 * 的时候，是不是没有发送PC，那么弹奏试听就无用了").
 *
 * Called on the edit, not on a timer: a detent has to be heard at once, and the
 * sent_prog cache is updated here so the tick path does not repeat it. Whether
 * this track OWNS the channel is not asked - the player chose the program while
 * looking at this track, so it goes out even when another track will re-state
 * its own on the next tick (that is the same convergence the owner rule relies
 * on, one message later).
 */
void ds_send_prog(ds_state *s, uint8_t trk)
{
    ds_track *t = &s->pat.track[trk & 15u];
    uint8_t ch = (uint8_t)(t->channel & 0x0Fu);
    /* Same rule as the tick path: state the program when the receiver's copy
     * differs. Walking a channel's tracks with SELECT therefore sends once, not
     * once per track, and a program edited back and forth does not spam a module
     * that is mid-note. sent_prog starts at 0xFF, so the first state always goes. */
    if (s->sent_prog[ch] == t->prog)
        return;
    emit_pc(s, t);                               /* bounds-checked; 4096 B out,
                                                  * and one edit queues 2 */
    s->sent_prog[ch] = t->prog;
}

/* land on a position: fire its voices per on/cond/prob */
static void land(ds_state *s, ds_track *t, int trk, uint32_t pos) {
    ds_step *st = &t->steps[pos & 63];
    if (st->tie) return;                 /* a tie never attacks: it extends the
                                          * attack that is already sounding */
    if (!st->on || !st->nvoices) return;
    if (!cond_ok(st, s, trk)) return;
    {
        uint32_t r = ds_rng_next(&s->rng);
        if ((int)(r % 100u) < (int)st->prob) {
            int vi;
            /* MONO: one voice per track, and it is the TOP pitch of the step
             * (a chord played into a mono track should sound its melody, not
             * an arbitrary member of the chord). The previous note is offed
             * first: without that the old note has no off at all, because its
             * gate entry got replaced — the classic mono hang. */
            int first = 0, last = st->nvoices;
            if (t->voice_mode == DS_VOICE_MONO) {
                int hi = 0;
                for (vi = 1; vi < st->nvoices; vi++)
                    if (st->v[vi].note > st->v[hi].note) hi = vi;
                first = hi;
                last = hi + 1;
                if (s->mono_note[trk] != 0xFF && s->mono_note[trk] != st->v[hi].note) {
                    if (s->out_len + 3 <= sizeof s->out) {
                        s->out[s->out_len++] = 0x80 | (t->channel & 0x0F);
                        s->out[s->out_len++] = s->mono_note[trk];
                        s->out[s->out_len++] = 0;
                    }
                    s->mono_note[trk] = 0xFF;
                }
            }
            for (vi = first; vi < last; vi++) {
                emit_note(s, t, &st->v[vi]);
                if (t->voice_mode == DS_VOICE_MONO)
                    s->mono_note[trk] = st->v[vi].note;
                {   /* gate: steps of this division, 0xFF = until next event.
                     * Tie steps never attack, so a run of them after this one
                     * belongs to this note: count them into the length. */
                    double g = (double)st->v[vi].gate;
                    uint32_t rel = (pos + 64 - t->loop_start) & 63;
                    uint32_t span = (uint32_t)(t->loop_len - rel);
                    if (st->v[vi].gate == 0xFF) {
                        uint32_t d;
                        g = (double)span;
                        for (d = 1; d < span; d++) {
                            ds_step *nx = &t->steps[(t->loop_start + rel + d) & 63];
                            if (nx->tie) continue;          /* extend, do not cut */
                            if (nx->on && nx->nvoices) { g = (double)d; break; }
                        }
                    } else {
                        uint32_t d;
                        for (d = 1; d < span && (double)d < g + 63.0; d++) {
                            ds_step *nx = &t->steps[(t->loop_start + rel + d) & 63];
                            if (!nx->tie) break;
                            g += 1.0;                       /* each tie step adds a cell */
                        }
                    }
                    gate_schedule(s, t, (uint8_t)trk, &st->v[vi], g);
                }
            }
        }
    }
    if (st->cond == DS_C_FILL) s->fill_flag = 0;    /* one-shot consumed */
}

/* Which track speaks for `ch`'s program right now, 0xFF = none. Same rule the
 * engine uses when it emits, exposed so the page can say so instead of letting
 * the player turn a knob that changes nothing. */
int ds_channel_owner(const ds_state *s, int ch)
{
    uint8_t owner[16];
    int i, any_solo = 0;
    for (i = 0; i < DS_TRACKS; i++)
        if (s->pat.track[i].solo) { any_solo = 1; break; }
    build_owners(s, owner, any_solo);
    if (ch < 0 || ch > 15) return 0xFF;
    return owner[ch] == 0xFF ? 0xFF : (int)owner[ch];
}

void ds_tick(ds_state *s, double dt_seconds) {
    int trk;
    uint8_t owner[16];
    ds_route_sync(s);                        /* ROUTE is editable live: 16 iterations, once a block */
    {   /* solo is a live-only mask: recompute the union each tick (16 bytes) */
        int t2;
        s->any_solo = 0;
        for (t2 = 0; t2 < DS_TRACKS; t2++)
            if (s->pat.track[t2].solo) { s->any_solo = 1; break; }
    }
    jam_advance(s, dt_seconds);              /* the jam ring's clock never stops: PLAY keeps
                                              * it on the bar line (same rate as tick_acc),
                                              * stop walks it free at the current BPM */
    /* live held notes: no auto-gate — the keyboard sustains while held and the key-up
     * sends the off (John 2026-10-28, option A). The 2-cell gate killed notes under a
     * held mouse/piano key after 250 ms (2×DIV_16 cell), which reads as a broken
     * instrument. A missed key-up (page hidden, focus loss) still dies: ds_stop sends
     * CC123 on every track channel and clears live[]. */
    if (!s->playing) { s->out_len = 0; return; }
    s->out_len = 0;
    /* absolute time @24 PPQ — the hero's axis. 4/4 bar = 16 sixteenths = 96 ticks. */
    s->tick_acc += dt_seconds * (s->bpm / 60.0) * 24.0;
    /* sequenced note-offs first: a gate that expires this tick must land in
     * the same byte stream as the note-on, or the note hangs. */
    gate_advance(s, dt_seconds);

    build_owners(s, owner, (int)s->any_solo);
    for (trk = 0; trk < DS_TRACKS; trk++) {
        ds_track *t = &s->pat.track[trk];
        double adv;
        if (t->mute)
            continue;
        if (s->any_solo && !t->solo)
            continue;                                 /* a solo elsewhere masks this track */
        /* PROGRAM CHANGE: per channel, not per track. The receiver keeps one
         * program per channel, so the channel's owner is the lowest-numbered
         * unmuted track that actually has notes on it - a track that is empty
         * has no sound to protect and must not steal the channel's program
         * (eleven never-configured tracks used to overwrite the demo's bass
         * with prog 0 in the same tick). Re-derived every tick from the
         * pattern, so a pattern switch, an edited CH/CHANNEL or a freshly
         * unmuted track all converge without extra bookkeeping.
         * Sent only when the receiver's copy differs: re-stating a program
         * mid-note is legal but pointless, and on some modules audible. */
        {
            uint8_t ch = (uint8_t)(t->channel & 0x0F);
            if (owner[ch] == (uint8_t)trk && s->sent_prog[ch] != t->prog) {
                emit_pc(s, t);
                s->sent_prog[ch] = t->prog;
            }
        }

        if (s->pending[trk]) {                       /* launch: loop_start sounds now */
            land(s, t, trk, s->pos16[trk]);
            s->pending[trk] = 0;
        }

        adv = dt_seconds / cell_seconds(s->bpm, t->division);
        /* swing: cells leading INTO odd steps are longer, odd->even shorter,
         * pair stays 2 cells (downbeat never slides). global × per-track, 100 => 3:1 */
        if (s->global_swing && t->swing_amt) {
            double sw = (s->global_swing / 100.0) * (t->swing_amt / 100.0) * 0.5;
            adv *= (s->pos16[trk] & 1u) ? (1.0 - sw) : (1.0 + sw);
        }
        s->frac16[trk] += adv;

        while (s->frac16[trk] >= 1.0) {
            uint32_t start = t->loop_start, len = t->loop_len;
            uint32_t cur, rel, next;
            s->frac16[trk] -= 1.0;
            cur = s->pos16[trk];
            rel = (cur + 64 - start) & 63;
            if (rel >= len)          next = start;                     /* outside: snap (live morph) */
            else if (rel + 1 >= len) { next = start; s->chain_cycle[trk]++; }  /* wrap: pass done */
            else                     next = (start + rel + 1) & 63;
            s->pos16[trk] = next;
            land(s, t, trk, next);
        }
    }
}

/* ---------- realtime jam: the always-armed scratch ring ----------
 *
 * "选了空白轨就能 jam，不按 REC" — recording is not a mode, so nothing gates
 * the capture: every audition the player hears lands in the ring quantized to
 * the ring's own division (1/16, unchanged by the tracks). The ring is one
 * ds_track, so SAVE is a plain copy/merge of tracks and the painter can draw
 * it like any other. Position = an absolute 1/16 counter since the last page
 * turn (PLAY from stop, SAVE success, CLEAR); while the transport runs the
 * counter rides the same clock as tick_acc so step 0 stays on the bar line,
 * and while stopped it walks on its own at the current BPM.
 */
static void jam_advance(ds_state *s, double dt)
{
    s->jam_acc += dt / cell_seconds(s->bpm, s->jam.division);
    if (s->jam_acc >= 1.0) {
        uint32_t n = (uint32_t)s->jam_acc;
        s->jam_acc -= (double)n;
        s->jam_abs = (s->jam_abs + n) & 0xFFFFu;         /* 64-wide wrap, no head/tail */
    }
}
uint32_t ds_jam_pos(const ds_state *s) { return s->jam_abs & 63u; }

void ds_jam_reset(ds_state *s)
{
    ds_clear_track(&s->jam);                 /* division back to DS_DIV_16, the ring's grid */
    s->jam.loop_len = 64;
    s->jam_held_n = 0;
    s->jam_abs = 0;
    s->jam_acc = 0.0;
    memset(s->jam_slot_pass, 0xFF, sizeof s->jam_slot_pass);   /* no slot in pass 0 yet */
}

/* find the voice a held note landed on and finalize its gate */
static void jam_set_gate(ds_track *t, uint8_t note, uint32_t on_abs, uint8_t gate)
{
    uint32_t pos = on_abs & 63u;
    uint8_t vi;
    for (vi = 0; vi < t->steps[pos].nvoices; vi++)
        if (t->steps[pos].v[vi].note == note) { t->steps[pos].v[vi].gate = gate; return; }
}

void ds_jam_capture(ds_state *s, uint8_t note, uint8_t vel, uint8_t down)
{
    int i;
    if (down) {
        uint32_t pos = ds_jam_pos(s);
        uint8_t pass = (uint8_t)(s->jam_abs >> 6);
        /* the ring holds ONE take — the current pass (jam_abs>>6). A hit into a
         * slot from an older pass rewrites the whole slot: the previous pass's
         * chord there dies with it (John 2026-10-28: C E G, then a pass later
         * A B A B, must save as A B A B alone). Within the pass, a re-hit
         * rewrites only its pitch — chord mates survive; the plain add would
         * swallow the re-hit (-2 dedupe) and leave the old vel/gate playing. */
        if (s->jam_slot_pass[pos] != pass) {
            s->jam.steps[pos].nvoices = 0;
            s->jam.steps[pos].on = 0;
        }
        s->jam_slot_pass[pos] = pass;
        ds_voice_remove(&s->jam, pos, note);
        ds_voice_add(&s->jam, pos, note, vel, 1);
        for (i = 0; i < s->jam_held_n; i++)
            if (s->jam_held[i].note == note) { s->jam_held[i].on = s->jam_abs; return; }
        if (s->jam_held_n < 8) {
            s->jam_held[s->jam_held_n].note = note;
            s->jam_held[s->jam_held_n].on = s->jam_abs;
            s->jam_held_n++;
        }
    } else {
        for (i = 0; i < s->jam_held_n; i++) {
            if (s->jam_held[i].note != note)
                continue;
            {   /* the note ends where the finger lifted: whole cells between the
                 * slots, plus the fraction of the cell we are inside now,
                 * rounded — a 2.5-cell hold notates as 3 so the sound stops
                 * when the player's finger actually came up. */
                double len = (double)(s->jam_abs - s->jam_held[i].on) + s->jam_acc + 0.5;
                uint32_t g = (uint32_t)len;
                if (g > 64u) g = 64u;
                jam_set_gate(&s->jam, note, s->jam_held[i].on, (uint8_t)(g ? g : 1));
            }
            for (; i + 1 < s->jam_held_n; i++) s->jam_held[i] = s->jam_held[i + 1];
            s->jam_held_n--;
            return;
        }
    }
}

void ds_jam_close_held(ds_state *s)
{
    int i;
    for (i = 0; i < s->jam_held_n; i++) {
        double len = (double)(s->jam_abs - s->jam_held[i].on) + s->jam_acc + 0.5;
        uint32_t g = (uint32_t)len;
        if (g > 64u) g = 64u;
        jam_set_gate(&s->jam, s->jam_held[i].note, s->jam_held[i].on,
                     (uint8_t)(g ? g : 1));
    }
    s->jam_held_n = 0;
}

static uint8_t jam_ct(uint8_t div)
{
    static const uint8_t CT[5] = {12, 8, 6, 4, 3};       /* cell_ticks(DS_DIV_*) */
    return CT[div <= DS_DIV_32 ? div : DS_DIV_16];
}

/* the ring is always 1/16 (6 ticks); a target track's cells may be 12/8/4/3
 * ticks. A ring gate of N sixteenths is N*6 ticks = (N*6)/cell_ticks(target)
 * target cells, rounded to at least 1 — the same length in SECONDS. */
static uint8_t jam_gate_rescale(uint8_t gate16, uint8_t target_div)
{
    uint32_t ticks = (uint32_t)gate16 * 6u;
    uint32_t ct = jam_ct(target_div);
    uint32_t g = (ticks + ct - 1u) / ct;                 /* ceil: a held note stays held */
    return (uint8_t)(g ? g : 1);
}

/* merge: ring notes never overwrite — they land only where ds_voice_add
 * accepts them (same cell + same pitch is -2, the target wins). Target
 * settings (division, channel, prog, loop) are the target's. Ring positions
 * are 1/16 slots: on a coarser target they land on the containing cell. */
int ds_jam_save_merge(ds_state *s, uint8_t trk)
{
    ds_track *t;
    uint32_t k;
    uint8_t vi, pass = (uint8_t)(s->jam_abs >> 6);
    if (trk >= DS_TRACKS) return -1;
    t = &s->pat.track[trk];
    /* save the CURRENT pass (the take): slots are stamped with the pass they were
     * written in, a slot from an older pass is the previous take's residue and is
     * skipped. The pass begins at slot 0, so slot order IS play order — the take
     * lands where it was played, no rotation. */
    for (k = 0; k < DS_STEPS; k++) {
        const ds_step *js = &s->jam.steps[k];
        uint32_t dst;
        if (s->jam_slot_pass[k] != pass)
            continue;
        dst = (k * 6u) / jam_ct(t->division);            /* ring tick -> target cell */
        for (vi = 0; vi < js->nvoices; vi++)
            ds_voice_add(t, dst, js->v[vi].note, js->v[vi].vel,
                         jam_gate_rescale(js->v[vi].gate, t->division));
        if (js->nvoices && !t->steps[dst].on) t->steps[dst].on = 1;   /* revive silent cells */
    }
    ds_jam_reset(s);
    return 0;
}

int ds_jam_save_replace(ds_state *s, uint8_t trk)
{
    ds_track *t;
    if (trk >= DS_TRACKS) return -1;
    if (!ds_jam_holds_notes(s))
        return 1;                            /* an empty ring replaces nothing: MERGE may have
                                              * just landed notes here, and "REPLACE" with
                                              * nothing would silently erase them */
    t = &s->pat.track[trk];
    {   /* the target's identity (channel/prog/route/mute/swing/voice_mode)
         * survives; only the note content is the ring's. division follows the
         * RING: the player played what they heard, not the target's grid. */
        uint8_t keep_channel = t->channel, keep_prog = t->prog, keep_route = t->route,
               keep_mute = t->mute, keep_voice_mode = t->voice_mode, keep_swing = t->swing_amt;
        ds_track fresh;
        uint32_t k;
        uint8_t vi, pass = (uint8_t)(s->jam_abs >> 6);
        ds_clear_track(&fresh);
        fresh.channel = keep_channel; fresh.prog = keep_prog; fresh.route = keep_route;
        fresh.mute = keep_mute; fresh.voice_mode = keep_voice_mode;
        fresh.swing_amt = keep_swing;
        fresh.division = s->jam.division;                /* the ring's grid: what was played is what is kept */
        fresh.loop_len = 64;
        /* current pass only, slot order = play order (see merge) */
        for (k = 0; k < DS_STEPS; k++) {
            const ds_step *js = &s->jam.steps[k];
            if (s->jam_slot_pass[k] != pass)
                continue;
            for (vi = 0; vi < js->nvoices; vi++)
                ds_voice_add(&fresh, k, js->v[vi].note, js->v[vi].vel, js->v[vi].gate);
        }
        *t = fresh;
    }
    ds_jam_reset(s);
    return 0;
}

int ds_jam_save_new(ds_state *s, uint8_t *out_trk)
{
    uint8_t t;
    for (t = 1; t < DS_TRACKS; t++) {
        uint32_t i;
        uint8_t empty = 1;
        if (s->pat.track[t].channel == 9)
            continue;                                /* drums are the kit, not a jam target */
        for (i = 0; i < DS_STEPS && empty; i++)
            if (s->pat.track[t].steps[i].nvoices) empty = 0;
        if (!empty)
            continue;
        *out_trk = t;
        return ds_jam_save_replace(s, t);
    }
    return -1;                                       /* every melodic track has content */
}

uint8_t ds_jam_holds_notes(const ds_state *s)
{
    uint32_t i;
    uint8_t vi;
    for (i = 0; i < DS_STEPS; i++) {
        const ds_step *st = &s->jam.steps[i];
        for (vi = 0; vi < st->nvoices; vi++)
            if (st->v[vi].note != 0xFF) return 1;    /* count real notes, not gate skeletons */
    }
    return 0;
}
