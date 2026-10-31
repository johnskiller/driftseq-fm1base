/* tools/chordsound_probe.mjs — the chord layer sounds on the HERO too, proven
 * by the wire instead of by the screen.
 *
 * John 2026-10-31: "scale页面，opc on的弹奏内容没有进jam，回到hero后自动退出了ofc，
 * 应该保持的"
 *
 * My step-record fix (5f5e317) put the page gate on the KEY instead of on the
 * WRITE: scale_key_hook() began with "if (ui_mode != M_GRID && ui_mode != M_SCALE)
 * return 0", so on the hero the chord layer handed every key back to the plain
 * audition path. The setting stayed ON; the sound was taken away from it — which
 * from the player's seat reads as "OFC 自动退出了". sc_write_ok() is the gate now,
 * and only the two writers (sc_off_write, sc_grid_write) ask it.
 *
 * tests/chordsound.txt proves this on the host by reading jam_notes. This probe
 * proves it in the shipped wasm by reading the MIDI that leaves the instrument,
 * because the hero deliberately draws no live-note readout (the first draft of
 * this file asserted that it did, and measured 0 px — the hero is a picture of
 * the PATTERN, not of the keyboard). Sound is the only honest witness here.
 *
 * The harness is jam_sound_probe.mjs's, reused: load the wasm into a stubbed
 * worklet, drive frames, drain both the packet ring and the worklet's frames.
 *
 *   node tools/chordsound_probe.mjs
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
const btn = (mask) => { M.web_buttons(mask); settle(30); M.web_buttons(0); settle(180); };
/* 260 ms hold: the emu's synthetic sense capture needs ~125 ms before it emits
 * the ON event. A shorter hold is aborted by the release and never registers. */
const hold = (k, vel = 100) => { M.web_key(k, vel, 0); settle(260); M.web_key(k, 0, 0); settle(150); };
const enc = (role, n) => { p.onMessage({ type: 'enc', role, n }); settle(60); };

settle(600);
btn(1 << 9);                                     /* SEQ -> GRID */
for (let i = 0; i < 3; i++) enc(0, 1);           /* track 3: the first melodic one */
btn(1 << 1);                                     /* SEL -> SCALE */
enc(5, 1);                                       /* K3: CHORD KEYS OFF -> GATE */
enc(3, 1);                                       /* K1: CHROMATIC -> MAJOR */

/* A component key (key 24 = G5) selects the third of the chord; without one the
 * layer plays the root alone by design, so a "chord" test needs this press. */
drain();
hold(24, 60);

/* ---- 1. the chord, played from the SCALE page ---- */
drain();
hold(14, 100);                                   /* key 14 = C5 (note 67) */
const fromScale = onsOf([...drain(), ...drain()]);
check(fromScale.length >= 3,
      'the SCALE page sounds a chord from one key', JSON.stringify(fromScale));
check(fromScale.some(([n]) => n === 67),
      'and the played note is its root', JSON.stringify(fromScale.slice(0, 6)));

/* ---- 2. the same chord, played from the HERO ---- */
/* This is the regression: with the gate on the key, these presses produced the
 * bare key and the chord layer never ran. */
btn(1 << 6);                                     /* HOME */
drain();
hold(14, 100);
const fromHero = onsOf([...drain(), ...drain()]);
check(fromHero.length >= 3,
      'the HERO sounds a chord from one key too', JSON.stringify(fromHero));
/* "same chord" means the same NOTES, not merely that the root is present: the
 * broken build also sent 67, alone. */
check(JSON.stringify(fromHero.map(([n]) => n).sort()) === JSON.stringify(fromScale.map(([n]) => n).sort())
      && fromHero.length === fromScale.length,
      'and it is the same chord, note for note - not a pass-through note',
      JSON.stringify(fromHero.slice(0, 6)) + ' vs ' + JSON.stringify(fromScale.slice(0, 6)));

/* ---- 3. the setting survives the trip (the "应该保持的" half) ---- */
/* The witness is the sound itself: after visiting the hero, playing on the
 * SCALE page must still produce a chord. If the setting had been cleared, this
 * would be one note. */
btn(1 << 1);                                     /* SEL -> SCALE */
drain();
hold(14, 100);
const back = onsOf([...drain(), ...drain()]);
check(back.length >= 3,
      'and the setting is still on after the hero (still a chord)', JSON.stringify(back));

/* ---- 4. the hero sounds but does not record ---- */
/* Sounding everywhere must not turn every page into a recorder. The witness is
 * the GRID itself: capture it, play the chord on the hero, come back, and the
 * cells must be byte-identical. A write would both fill a cell and move the
 * cursor, so even one stray write changes the frame. (PLAY cannot witness this:
 * the demo pattern already contains note 67 on its own tracks.) */
const grab = () => {
    const fb = new Uint16Array(M.memory.buffer, M.web_fb(), 240 * 240);
    return Array.from(fb);
};
/* Compare only the CELL area, y 0..211. A page round-trip changes the bottom
 * three rows (y212..214) all by itself — measured: an empty trip to the hero and
 * back differs by 611 px with nothing played — so a whole-frame comparison would
 * report a write that did not happen. */
const diff = (a, b) => {
    let n = 0;
    for (let y = 0; y < 212; y++)
        for (let x = 0; x < 240; x++) {
            const i = y * 240 + x;
            if (a[i] !== b[i]) n++;
        }
    return n;
};

btn(1 << 9);                                     /* SEQ -> GRID */
settle(400);
const gridBefore = grab();
btn(1 << 6);                                     /* HOME */
settle(300);
hold(14, 100);                                   /* the chord, on the hero */
btn(1 << 9);                                     /* back to GRID */
settle(400);
const gridAfter = grab();
check(diff(gridBefore, gridAfter) === 0,
      'a chord played on the hero wrote no step',
      diff(gridBefore, gridAfter) + ' px differ');

/* and, for contrast, the same chord on the GRID page DOES write - so the check
 * above is measuring the gate, not a dead key path. */
hold(14, 100);
settle(300);
const gridWritten = grab();
check(diff(gridAfter, gridWritten) > 0,
      'the same chord on the GRID page does write (the gate is not a mute)',
      diff(gridAfter, gridWritten) + ' px differ');

console.log('\n' + (fails ? 'CHORD SOUND PROBE FAILED (' + fails + '/' + checks + ')'
                          : 'CHORD SOUND PROBE PASSED (' + checks + ' checks)'));
process.exit(fails ? 1 : 0);
