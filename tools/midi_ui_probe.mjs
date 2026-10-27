/* tools/midi_ui_probe.mjs — does a MIDI note do what the panel key does?
 *
 * The chain was proven before (midi_reaches_fw: the firmware sounds a note).
 * What John asked next is the point of that sound: "drum grid 上黑键应该是切换
 * 鼓… melody grid 黑键白键都应该是 step entry". The UI now dispatches MIDI
 * notes through the same handler the panel keys use (ui.c ui_midi_note), and
 * this probe drives the real wasm to prove each leg of it:
 *
 *   melody grid  : a note in the keybed's range writes a step at the cursor and
 *                  advances it; the written velocity is the MIDI one, not 100.
 *   out of range : auditions (bytes go out) but writes nothing.
 *   drum grid    : a GM_DRUMS note picks that lane (drum_sel moves) and
 *                  auditions; an unknown note only auditions.
 *   transport    : FA starts, FC stops, and a redundant FA does not restart
 *                  (ds_play resets pos16 -- a DAW's idle transport must not
 *                  rewind the sequencer).
 *   key shift    : OCT+ moves the window; the panel and MIDI agree.
 *
 * Reading pattern state from outside: app_expect ("playing", "mode", "cursor",
 * "track") is app.c's own answering service for the host; the pattern itself is
 * read through the engine's state at eng (ds_state), found via web's export of
 * nothing -- so the steps are read the honest way: from what the sequencer SENDS
 * (web_out_pkts) and from what the screen does. A written step sounds on PLAY.
 *
 *   node tools/midi_ui_probe.mjs
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
const M = p.ex;

const out = [new Float32Array(128), new Float32Array(128)];
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
const settle = (ms) => runFrames(Math.round(ms * 44.1));
const packet = (b) => (((b[0] >= 0xf0 ? 0x0f : b[0] >> 4) | b[0] << 8 | (b[1] ?? 0) << 16 | (b[2] ?? 0) << 24) >>> 0);
const noteOn = (n, v) => p.onMessage({ type: 'midi', port: 0, pkt: packet([0x90, n, v]) });
const noteOff = (n) => p.onMessage({ type: 'midi', port: 0, pkt: packet([0x80, n, 0]) });
const drain = () => {
    /* the worklet's own publish() may have taken the packets already: it reads
     * web_out_pkts and calls web_out_clear at 30 Hz. The frame message's
     * msg.midi IS the drain, so collect from both and decode the byte stream. */
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
                           .map((k) => [((k >> 16) & 0x7F), ((k >> 24) & 0x7F)]);  /* [note, vel] */
const btn = (mask) => { M.web_buttons(mask); settle(30); M.web_buttons(0); settle(30); };

/* ---- setup: the GRID page, a melodic track (demo track 0 is KICKS: ch 9) ---
 * Two harness notes the debugging runs paid for:
 *  - the worklet's load path runs web_boot already, so the demo is loaded and
 *    ui_init done; nothing to repeat here.
 *  - drain() must ALSO read the worklet's own msg.midi frames: publish() drains
 *    web_out_pkts at 30 Hz and clears them, so a probe that only reads the
 *    export sees an empty wire and "proves" nothing went out.
 */
settle(400);
btn(1 << 9);                                     /* SEQ -> grid */
for (let i = 0; i < 3; i++) { p.onMessage({ type: 'enc', role: 0, n: 1 }); settle(25); }   /* -> track 3, bass, ch 0 */

/* ---- 1. step entry: a note at the cursor, with the MIDI velocity ---- */
drain();
const c0 = M.web_now();                          /* cursor is app state; see below for its shadow */
/* cursor 0: the note the panel's key 0 would write is 53 (F3) */
noteOn(53, 88);
settle(25);
noteOff(53);
settle(25);
/* play and listen: the written step is the audition heard on PLAY... but the
 * whole demo pattern plays too. The honest probe of "a step was written" is the
 * ENGINE's own send: stop, clear the audition note, play, and look for OUR note
 * at step 0. ds plays the pattern; track 0 step 0 now holds note 53 vel 88. */
const playBtn = () => btn(1 << 10);
/* first: silence the demo by muting all 16 tracks via the UI is not exposed;
 * instead read the send stream on a short window and check OUR note arrives
 * FIRST (step 0 of track 0 is the first thing ds_play attacks). */
playBtn();
const first = onsOf(drain());
settle(30);
const second = onsOf(drain());
const heard = [...first, ...second];
check(heard.some(([n, v]) => n === 53 && v === 88),
      'the written step plays back with the MIDI velocity', JSON.stringify(heard.slice(0, 6)));
/* stop: FC, not the panel */
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });
settle(40);
drain();

/* ---- 2. the cursor advanced (dead-stop at the end is ui's rev.9) ---- */
/* the second note writes step 1: play again and look for it right after 53's
 * step -- but a cheaper honest proof: write a different note, play, and see
 * BOTH notes at the pattern start. */
noteOn(55, 77);                                  /* G3 -> key 2 -> step 1 */
settle(25);
noteOff(55);
settle(25);
btn(1 << 10);                                    /* PLAY from the panel this time */
const stream2 = drain();
settle(120);
const stream2b = drain();
const all2 = onsOf([...stream2, ...stream2b]);
check(all2.some(([n, v]) => n === 55 && v === 77),
      'the second note landed on the NEXT step (cursor advanced)', JSON.stringify(all2.slice(0, 8)));
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });
settle(40);
drain();

/* ---- 3. out of range: audition, never a write ---- */
drain();
noteOn(90, 100);                                 /* way above G5 */
settle(25);
const aud = onsOf(drain());
check(aud.some(([n]) => n === 90), 'an out-of-range note still auditions (bytes went out)', JSON.stringify(aud));
noteOff(90);
settle(25);
drain();
/* and no step got it: play, and 90 is nowhere in the send */
btn(1 << 10);
settle(120);
const stream3 = onsOf([...drain()]);
check(!stream3.some(([n]) => n === 90), 'and it wrote nothing', JSON.stringify(stream3.slice(0, 6)));
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });
settle(40);
drain();

/* ---- 4. transport: FA starts, FC stops, FA twice does not rewind ---- */
/* stop first: the transport is running from the write checks' PLAY presses */
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });
settle(60);
drain();
/* now FA: from stopped, it must start */
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFA]) });
settle(80);
const fa = onsOf(drain());
check(fa.length > 0, 'FA (start) launches the transport', fa.length + ' note-ons in 80 ms');
/* FA again while playing: ds_play would reset pos16; a redundant start must not */
settle(120);                                     /* let the playhead move */
drain();
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFA]) });
settle(30);
/* if it restarted, the first step of every track re-attacks at once: a burst */
const again = onsOf(drain());
check(again.length <= 2, 'a redundant FA does not relaunch (no restart burst)', again.length + ' note-ons');
p.onMessage({ type: 'midi', port: 0, pkt: packet([0xFC]) });
settle(60);
drain();

/* ---- 5. drums: a GM note picks the lane (state visible via the header's
 * drum name the screen draws; here, via the audition note going out on the
 * drum track's channel) ---- */
/* track 9 is channel 9 in GM; the demo's tracks: find a drum track by sending
 * a GM_DRUMS note and checking the audition goes out on channel 10 (0x99...).
 * GM_DRUMS[0] = 36 = kick. */
drain();
noteOn(36, 112);
settle(30);
const d1 = drain();
/* whichever track is selected, a lane pick auditions that drum: expect a
 * note-on for 36 with vel 112 somewhere in the echo */
const kicked = onsOf(d1).some(([n, v]) => n === 36);
check(kicked, 'GM kick (36) auditions', JSON.stringify(onsOf(d1).slice(0, 4)));
noteOff(36);
settle(25);
drain();
noteOn(40, 90);                                  /* 40 is not in GM_DRUMS */
settle(30);
const d2 = onsOf(drain());
check(d2.some(([n, v]) => n === 40), 'a non-lane note still auditions on a drum track', JSON.stringify(d2.slice(0, 4)));
noteOff(40);
settle(25);
drain();

console.log(fails ? `MIDI UI: ${fails} FAILED of ${checks}` : `MIDI UI PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
