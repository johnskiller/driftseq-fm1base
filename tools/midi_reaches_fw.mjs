/* tools/midi_reaches_fw.mjs — does a MIDI message reach the FIRMWARE, and does
 * the firmware do anything with it?
 *
 * "所有midi事件都没有进入FW处理" cannot be answered from the page: the page draws a
 * key, prints a monitor line and can play its own square wave without the wasm
 * ever noticing. This probe never touches the page. It loads app.wasm into the
 * real worklet and reads the two things only the firmware produces:
 *
 *   - the firmware's own frame buffer (web_fb), i.e. what it drew;
 *   - the firmware's own audio (web_out_l/r), i.e. what it played.
 *
 * Deliberately NOT the screen-reader channel (announce.c / web_said_*): that
 * needs a HELLO handshake, is off unless a reader asks, and is not part of the
 * MIDI path. Using it here made an earlier version of this probe fail for a
 * reason that had nothing to do with MIDI.
 *
 *   node tools/midi_reaches_fw.mjs
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
check(!!p.ex, 'the worklet booted the firmware');

const out = [new Float32Array(128), new Float32Array(128)];
/* last sample of the window, not the peak: a release starts at full level and
 * decays, so a peak over a window that opens before the note off is the note
 * itself. (That trap cost two runs.) */
const runFrames = (n) => {
    let last = 0;
    for (let i = 0; i < n; i += 128) {
        out[0].fill(0); out[1].fill(0);
        p.process([], [out, out]);
        for (let k = 0; k < 128; k++) last = Math.abs(out[0][k]);
        ctx.currentTime += 128 / 44100;
    }
    return last;
};
const packet = (b) => (((b[0] >= 0xf0 ? 0x0f : b[0] >> 4) | b[0] << 8 | (b[1] ?? 0) << 16 | (b[2] ?? 0) << 24) >>> 0);

/* The firmware's frame buffer, hashed. The app redraws on its own schedule, so
 * this is "what the firmware drew last", not "what the page displayed". */
const screen = () => {
    const fb = new Uint16Array(p.mem.buffer, p.ex.web_fb(), 240 * 240);
    let h = 2166136261;
    for (let i = 0; i < fb.length; i++) { h ^= fb[i]; h = Math.imul(h, 16777619); }
    return h >>> 0;
};
/* let the firmware's UI run: it draws every 16 ms of its own clock */
const settle = () => runFrames(44100 * 0.25);

runFrames(44100 * 0.6);                       /* boot, splash, first frame */
settle();
const fbIdle = screen();
check(fbIdle !== 0, 'the firmware is drawing its own screen', fbIdle.toString(16));

/* ---- 1. a note: the firmware plays it ---- */
check(runFrames(2000) < 0.001, 'silent before anything is sent');
await p.onMessage({ type: 'midi', port: 0, pkt: packet([0x90, 0x45, 0x64]) });
const heard = runFrames(4410);
check(heard > 0.005, 'a note in makes the FIRMWARE play (its own output, not the page\'s)',
      heard.toFixed(4));
await p.onMessage({ type: 'midi', port: 0, pkt: packet([0x80, 0x45, 0x00]) });
check(runFrames(8820) < 0.002, 'and its note off silences it');

/* ---- 2. a CC: the firmware does not play it, and this firmware has no CC
 * handler at all -- app.c's midi_in() reads notes and nothing else. Recorded
 * here so nobody expects sound from a CC; the page maps CC onto the panel. ---- */
const beforeCc = runFrames(2000);
await p.onMessage({ type: 'midi', port: 0, pkt: packet([0xb0, 0x14, 0x7f]) });
check(Math.abs(runFrames(4410) - beforeCc) < 0.002, 'a bare CC makes no sound (no CC handler in the firmware)');

/* ---- 3. what the page maps a CC to: the firmware's own encoder input ----
 * The page turns CC20-26 into { type: "enc" } and CC40-53 into { type: "buttons" },
 * which is how a knob on the FM-1 actually reaches this app. If that works, the
 * firmware's screen changes -- its footer shows the knob's value. */
const fbBefore = screen();
await p.onMessage({ type: 'enc', role: 3, n: 4 });      /* K1, four detents */
settle();
check(screen() !== fbBefore, 'an encoder turn changes what the FIRMWARE draws', `${fbBefore.toString(16)} -> ${screen().toString(16)}`);

/* ---- 4. and a note after all that still plays: the queue is not stuck ---- */
await p.onMessage({ type: 'midi', port: 0, pkt: packet([0x90, 0x48, 0x64]) });
check(runFrames(4410) > 0.005, 'a note after a CC and an encoder turn still plays');

console.log(fails ? `MIDI TO FW: ${fails} FAILED of ${checks}` : `MIDI TO FW PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
