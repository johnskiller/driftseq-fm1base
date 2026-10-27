/* tools/worklet_link_probe.mjs — the whole path: page message -> worklet -> wasm.
 *
 * The page's monitor proves a message arrived AT THE PAGE; the flash the page
 * draws proves nothing about the firmware. This probe closes that gap: it loads
 * the real app.wasm into the real worklet.js (with the worklet globals stubbed)
 * and sends the same { type: "midi" } message the page posts, then reads the
 * wasm's own audio output. If a note in is not heard there, it never reached the
 * app -- which is exactly the question "所有midi事件都没有进入FW处理" asks.
 *
 *   node tools/worklet_link_probe.mjs
 */
import fs from 'node:fs';
import vm from 'node:vm';

let checks = 0, fails = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
};

/* ---- the worklet, with the two globals it needs ---- */
const posted = [];
class AudioWorkletProcessor {
    constructor() {
        this.port = { postMessage: (m) => posted.push(m), onmessage: null };
        this.ex = null;
        this.frames = 0;
        this.busy = 0;
        this.lastFrame = 0;
        this.sentBlits = -1;
        this.sentWrites = -1;
    }
}
let Proc = null;
const ctx = { AudioWorkletProcessor, registerProcessor: (n, c) => { Proc = c; }, currentTime: 0, console };
ctx.globalThis = ctx;
ctx.performance = { now: () => Date.now() };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(new URL('../web/emu/worklet.js', import.meta.url), 'utf8'), ctx);

const wasm = fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url));
const p = new Proc();

/* the worklet's own "load" path: this is what buildAudio posts */
await p.onMessage({ type: 'load', wasm: wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength), store: {}, master: 2800 });
check(!!p.ex, 'the worklet loaded app.wasm');
check(typeof p.ex.web_midi_in === 'function', 'and the wasm exports web_midi_in');

/* ---- render, and watch the app's own audio ---- */
/* process() writes 128 frames into out[0]/out[1] from the wasm's ring */
const out = [new Float32Array(128), new Float32Array(128)];
/* The tail, not the peak: a release starts at full level and decays, so a peak
 * over a window that begins before the note-off is the note itself. (Same trap
 * midi_probe hit: measuring the attack and calling it the decay.) */
const render = (frames) => {
    let last = 0, peak = 0;
    for (let i = 0; i < frames; i += 128) {
        out[0].fill(0); out[1].fill(0);
        p.process([], [out, out]);
        for (let k = 0; k < 128; k++) {
            const a = Math.abs(out[0][k]);
            if (a > peak) peak = a;
            last = a;
        }
        ctx.currentTime += 128 / 44100;
    }
    return last;
};
const settle = () => { for (let i = 0; i < 40; i++) { out[0].fill(0); out[1].fill(0); p.process([], [out, out]); ctx.currentTime += 128 / 44100; } };

render(44100 * 0.3);
check(render(2000) < 0.001, 'silent before anything is sent');

/* ---- the page's message, exactly as send() posts it ---- */
const packet = (bytes) => {
    const st = bytes[0], d1 = bytes[1] ?? 0, d2 = bytes[2] ?? 0;
    return ((st >= 0xf0 ? 0x0f : st >> 4) | st << 8 | d1 << 16 | d2 << 24) >>> 0;
};
await p.onMessage({ type: 'midi', port: 0, pkt: packet([0x90, 69, 100]) });
const heard = render(4410);                       // 100 ms
check(heard > 0.005, 'a note-on posted as { type: "midi" } is heard in the app\'s output', heard.toFixed(4));

await p.onMessage({ type: 'midi', port: 0, pkt: packet([0x80, 69, 0]) });
const after = render(8820);
check(after < 0.002, 'and its note-off stops it', after.toFixed(5));

/* ---- CC: what the firmware does with it ---- */
/* There is no CC -> knob or CC -> button mapping anywhere in this firmware.
 * app.c's midi_in() reads notes and nothing else; the old tree's midi_io.c sent
 * CC to leds_on_cc. So a CC arrives and the app legitimately does nothing, which
 * is what "旋钮和其他按钮没有反馈" is seeing. This check pins that fact so the
 * page keeps its own CC mapping (as the old emu had) rather than waiting on the
 * firmware for something it does not implement. */
const before = render(1000);
await p.onMessage({ type: 'midi', port: 0, pkt: packet([0xb0, 20, 127]) });
const afterCc = render(4410);
check(Math.abs(afterCc - before) < 0.002, 'a CC changes nothing in the app\'s sound (there is no CC handler)', afterCc.toFixed(4));

/* it does reach the wasm's queue, though: a note after it still sounds, so the
 * ring is not stuck on an unhandled message */
await p.onMessage({ type: 'midi', port: 0, pkt: packet([0x90, 72, 100]) });
const next = render(4410);
check(next > 0.005, 'a note after a CC still plays (the queue is not blocked)', next.toFixed(4));
settle();

console.log(fails ? `WORKLET LINK: ${fails} FAILED of ${checks}` : `WORKLET LINK PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
