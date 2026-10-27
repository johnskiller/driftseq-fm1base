/* tools/jam_sound_probe.mjs — the jam ring's DATA path, proven by the wire:
 *
 *   1. jam a note on the HOME page (velocity 77 via web_key, the touch path)
 *   2. PLAY: the demo pattern sounds but NOTE 53 must NOT sound from the ring
 *      (the ring is scratch; only MERGE/REPLACE/NEW TRK writes a track)
 *   3. STOP, SAVE -> MERGE (menu: open, confirm)
 *   4. PLAY again: note 53 sounds NOW, with velocity 77 — the ring landed in
 *      track 5 (the selected melodic track) and the real velocity rode along
 *   5. REPLACE with an empty ring then PLAY: 53 still sounds (merge left it)
 *      — replace of an empty ring clears nothing that merge wrote
 *
 * Same harness lessons as midi_ui_probe: drain() must read BOTH web_out_pkts
 * and the worklet's msg.midi frames, and the demo pattern plays on any track
 * selection, so assertions look for OUR note specifically.
 */
import fs from 'node:fs';
import vm from 'node:vm';

let checks = 0, fails = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
};

const posted = [];
class AudioWorkletProcessor {
    constructor() {
        this.port = { postMessage: (m) => posted.push(m), onmessage: null };
        this.ex = null; this.frames = 0; this.busy = 0;
        this.lastFrame = 0; this.sentBlits = -1; this.sentWrites = -1;
    }
}
let Proc = null;
const ctx = { AudioWorkletProcessor, registerProcessor: (n, c) => { Proc = c; }, currentTime: 0, console };
ctx.globalThis = ctx;
ctx.performance = { now: () => Date.now() };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(new URL('../web/emu/worklet.js', import.meta.url), 'utf8'), ctx);

const wasmBytes = fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url));
const p = new Proc();
await p.onMessage({
    type: 'load',
    wasm: wasmBytes.buffer.slice(wasmBytes.byteOffset, wasmBytes.byteOffset + wasmBytes.byteLength),
    store: {}, master: 2800,
});
const M = p.ex;

const out = [new Float32Array(128), new Float32Array(128)];
const settle = (ms) => {
    const n = Math.round(ms * 44.1);
    for (let i = 0; i < n; i += 128) {
        out[0].fill(0); out[1].fill(0);
        p.process([], [out, out]);
        ctx.currentTime += 128 / 44100;
    }
};
const packet = (b) => (((b[0] >= 0xf0 ? 0x0f : b[0] >> 4) | b[0] << 8 | (b[1] ?? 0) << 16 | (b[2] ?? 0) << 24) >>> 0);
const drain = () => {
    const n = M.web_out_n();
    const pk = n ? Array.from(new Uint32Array(M.memory.buffer, M.web_out_pkts(), n)) : [];
    if (n) M.web_out_clear();
    const bytes = [];
    posted.splice(0).forEach((m) => { if (m.type === 'frame' && m.midi) bytes.push(...m.midi); });
    for (let i = 0; i < bytes.length; ) {
        const st = bytes[i];
        const len = st >= 0xf0 ? (st === 0xf2 ? 3 : 1) : (st & 0xf0) === 0xc0 || (st & 0xf0) === 0xd0 ? 2 : 3;
        pk.push(((st >= 0xf0 ? 0x0f : st >> 4) | st << 8 | (bytes[i + 1] ?? 0) << 16 | (bytes[i + 2] ?? 0) << 24) >>> 0);
        i += len;
    }
    return pk;
};
const onsOf = (pkts) => pkts.filter((k) => ((k >> 8) & 0xF0) === 0x90 && ((k >> 24) & 0x7F) > 0)
                           .map((k) => [((k >> 16) & 0x7F), ((k >> 24) & 0x7F)]);
const btn = (mask) => { M.web_buttons(mask); settle(30); M.web_buttons(0); settle(30); };
const keyVel = (k, vel) => { M.web_key(k, vel, 0); settle(260); M.web_key(k, 0, 0); settle(150); };
/* 260ms hold: the emu's synthetic sense capture needs ~125ms before it emits
 * the ON event (8 pre-samples + the 48-sample capture) — a shorter hold gets
 * aborted by the release and the key never registers (a real finger is never
 * this fast). */

/* ---- setup: GRID, select track 5 (first empty melodic) ---- */
settle(400);
btn(1 << 9);                                     /* SEQ -> grid */
for (let i = 0; i < 5; i++) { p.onMessage({ type: 'enc', role: 0, n: 1 }); settle(20); }   /* track 5 */

/* ---- 1. jam F3 (note 53 = key 0) at velocity 77, on the HOME page ---- */
btn(1 << 6);                                     /* HOME: audition-only path */
drain();
keyVel(0, 77);
drain();

/* ---- 2. PLAY: the ring must NOT sound ---- */
btn(1 << 10);                                    /* PLAY */
settle(60);
const ringQuiet = [...onsOf(drain()), ...onsOf(drain())];
check(!ringQuiet.some(([n]) => n === 53),
      'the ring does not sound on PLAY (scratch, not a track)', JSON.stringify(ringQuiet.slice(0, 5)));
settle(80); drain();
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });   /* STOP via FC */
settle(60); drain();

/* ---- 3. SAVE -> MERGE ---- */
btn(1 << 7);                                     /* SAVE: menu opens on any page */
settle(60);
btn(1 << 7);                                     /* confirm MERGE (sel defaults to 0) */
settle(60);
drain();

/* ---- 4. PLAY again: note 53 sounds NOW, with velocity 77 ----
 * The jam landed at the ring slot where it was played (anywhere in 64), and
 * the merged track keeps that slot: one loop at 120 BPM is 8 s, so wait a
 * full cycle before hunting for the note. */
btn(1 << 10);
settle(2000);
let merged = [...onsOf(drain()), ...onsOf(drain())];
settle(2000);
merged = [...merged, ...onsOf(drain()), ...onsOf(drain())];
settle(2000);
merged = [...merged, ...onsOf(drain()), ...onsOf(drain())];
settle(2000);
merged = [...merged, ...onsOf(drain()), ...onsOf(drain())];
settle(600); drain();
check(merged.some(([n, v]) => n === 53 && v === 77),
      'after MERGE, note 53 plays back at velocity 77', JSON.stringify(merged.slice(0, 8)));
settle(80); drain();
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });
settle(60); drain();

/* ---- 5. SAVE -> REPLACE with the ring now empty: 53 survives ---- */
btn(1 << 7);                                     /* menu */
settle(40);
p.onMessage({ type: 'enc', role: 4, n: 1 });     /* K2 detent: sel -> REPLACE(1) */
settle(40);
btn(1 << 7);                                     /* confirm REPLACE */
settle(60); drain();
btn(1 << 10);
settle(2000);
let afterRep = [...onsOf(drain()), ...onsOf(drain())];
settle(2000);
afterRep = [...afterRep, ...onsOf(drain()), ...onsOf(drain())];
settle(2000);
afterRep = [...afterRep, ...onsOf(drain()), ...onsOf(drain())];
settle(2000);
afterRep = [...afterRep, ...onsOf(drain()), ...onsOf(drain())];
settle(600); drain();
check(afterRep.some(([n]) => n === 53),
      'REPLACE (ring empty) leaves the merged note alone', JSON.stringify(afterRep.slice(0, 6)));
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });
settle(40); drain();

console.log(fails ? `JAM SOUND PROBE FAILED (${fails}/${checks})` : `JAM SOUND PROBE PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
