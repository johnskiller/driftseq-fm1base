/* tools/worklet_midi_probe.mjs — the worklet's MIDI out, without a browser.
 *
 * worklet.js runs inside an AudioWorklet, which node does not have. What can be
 * tested on its own is the one thing that would fail silently: publish() reading
 * the app's packets and unpacking them into bytes. A program change is 2 bytes,
 * everything else 3; a wrong length shifts every message after it and the
 * receiving synth plays nonsense, while this side looks perfectly healthy.
 *
 * The file is loaded as text and run with AudioWorkletProcessor /
 * registerProcessor stubbed, so the test exercises the shipped source and not a
 * copy of it.
 *
 *   node tools/worklet_midi_probe.mjs
 */
import fs from 'node:fs';
import vm from 'node:vm';

let checks = 0, fails = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
};

/* a fake wasm: publish() only reads these. One ArrayBuffer for everything, as
 * the wasm's memory is: web_fb() and web_out_pkts() are offsets into it. */
const mem = new ArrayBuffer(240 * 240 * 2 + 256 * 4);
const fb = new Uint16Array(mem, 0, 240 * 240);
const pkts = new Uint32Array(mem, 240 * 240 * 2, 64);
let nOut = 0, cleared = 0;
const ex = {
    web_blits: () => 1, web_fb: () => 0, web_lit_buttons: () => 0, web_lit_keys: () => 0,
    web_said_len: () => 0, web_said_buf: () => 0, web_said_clear: () => {},
    web_store_writes: () => 0, web_nobj: () => 0,
    web_out_n: () => nOut, web_out_pkts: () => 240 * 240 * 2, web_out_clear: () => { cleared++; nOut = 0; },
};
const posted = [];
class AudioWorkletProcessor {
    constructor() {
        this.port = { postMessage: (m, t) => posted.push(m), onmessage: null };
        this.ex = ex;
        this.mem = { buffer: mem };
        this.sentBlits = 1;              /* so publish() does not try to copy a framebuffer */
        this.sentWrites = 0;
        this.busy = 0;
        this.frames = 0;
        this.lastFrame = 0;
    }
}
let Proc = null;
const ctx = {
    AudioWorkletProcessor,
    registerProcessor: (name, cls) => { Proc = { name, cls }; },
    currentTime: 1,
    globalThis: null,
    performance: { now: () => 0 },
};
ctx.globalThis = ctx;
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(new URL('../web/emu/worklet.js', import.meta.url), 'utf8'), ctx);

check(Proc && Proc.name === 'fm1', 'the worklet registers the fm1 processor');

const p = new Proc.cls();
p.ex = ex;                        /* the processor nulls it until a "load" message */
p.mem = { buffer: mem };
p.sentBlits = 1;                  /* so publish() does not try to copy a framebuffer */
p.sentWrites = 0;
const put = (bytes) => {
    /* the page's own packing, as midi_probe.mjs asserts it */
    for (const b of bytes) {
        const [st, d1 = 0, d2 = 0] = b;
        pkts[nOut++] = (((st >= 0xf0 ? 0x0f : st >> 4) | st << 8 | d1 << 16 | d2 << 24) >>> 0);
    }
};
const publish = () => { posted.length = 0; p.publish(); return posted[0]; };

/* nothing out: no midi field at all, and no clear */
let m = publish();
check(m && m.type === 'frame' && m.midi === undefined, 'nothing sent: the frame carries no midi field');

put([[0x90, 60, 100], [0x80, 60, 0]]);
m = publish();
check(ArrayBuffer.isView(m.midi) && m.midi.constructor.name === 'Uint8Array', 'what the app sent arrives as bytes', m.midi.constructor.name);
check(m.midi && [...m.midi].join(',') === '144,60,100,128,60,0', 'a note on and a note off unpack to 6 bytes',
      m.midi && [...m.midi].join(','));
check(cleared >= 1 && nOut === 0, 'the ring is drained after reading it', 'in ring now ' + nOut);

put([[0xC0, 7], [0x90, 64, 90], [0xB0, 1, 64]]);
m = publish();
check([...m.midi].join(',') === '192,7,144,64,90,176,1,64',
      'a program change is 2 bytes, so it does not swallow the next status', [...m.midi].join(','));

put([[0xF8]]);
m = publish();
check([...m.midi].join(',') === '248', 'clock unpacks to its one byte', [...m.midi].join(','));

/* the whole point of the length rule: a receiver walking this must land on
 * status bytes and nothing else */
put([[0xC0, 1], [0xC1, 2], [0x9F, 40, 1], [0x8F, 40, 0], [0xE2, 0, 64]]);
m = publish();
const b = [...m.midi];
let i = 0, bad = 0;
while (i < b.length) {
    if (b[i] < 0x80) { bad++; i++; continue; }
    const t = b[i] & 0xf0;
    check(t < 0xf0, 'every message is a channel message here', '0x' + b[i].toString(16));
    i += t === 0xc0 || t === 0xd0 ? 2 : 3;
}
check(bad === 0, 'the stream parses from end to end', bad + ' orphaned bytes');

console.log(fails ? `WORKLET MIDI: ${fails} FAILED of ${checks}` : `WORKLET MIDI PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
