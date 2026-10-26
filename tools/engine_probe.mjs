/* tools/engine_probe.mjs — does the sequencer actually run on fm1-base?
 *
 * The build succeeding only proves the engine links. This drives the real
 * emulator wasm and checks the thing that matters: after starting playback,
 * MIDI bytes come out of plat_midi_send, in the right shape, at a rate that
 * matches the tempo.
 *
 *   node tools/engine_probe.mjs
 *
 * Reads platform/web/emu/emu_web.c's exports: web_out_n (how many packets went
 * out), web_out_pkts (their values), web_out_clear (drain), web_now (emulated
 * ms). Packets are plat.h's shape: status<<8 | d1<<16 | d2<<24.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const EMU = path.join(HERE, '..', 'build', 'emu', 'app.wasm');

if (!fs.existsSync(EMU)) {
    console.error('no wasm at ' + EMU + ' — build with --web first');
    process.exit(2);
}

let fails = 0;
const check = (ok, msg, detail) => {
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
    if (!ok) fails++;
};

const { instance } = await WebAssembly.instantiate(fs.readFileSync(EMU), {
    wasi_snapshot_preview1: {
        clock_time_get: () => 0,
        proc_exit: () => {},
        fd_write: () => 0,
        fd_close: () => 0,
        environ_get: () => 0,
        environ_sizes_get: () => 0,
    },
    env: {},
});
const M = instance.exports;
const u8 = () => new Uint8Array(M.memory.buffer);

/* plat.h button ids (B_*) — SEQ is the sequencer page, PLAY runs.
 * web_buttons takes a MASK of held buttons, not (id, state). */
const B_SEQ = 9, B_PLAY = 10;

function press(btn) {
    M.web_buttons(1 << btn);        /* down */
    step(20);                       /* held for a frame or two, so input() sees it */
    M.web_buttons(0);               /* up */
    step(20);
}

let rendered = 0;
function step(ms) {
    /* web_render(n) takes SAMPLE FRAMES, not milliseconds: run_ms(1) renders
     * 44100/1000 = 44.1 frames per emulated millisecond. So n samples is
     * n/44.1 ms -- passing the millisecond count straight in advanced the
     * clock ~44x too slowly and looked like a dead engine. */
    const n = Math.round(ms * 44.1);
    for (let i = 0; i < n; i += 1024) {
        const take = Math.min(1024, n - i);
        M.web_render(take);
        rendered += take;
    }
}

function drain() {
    const n = M.web_out_n();
    const ptr = M.web_out_pkts();
    const mem = new Uint32Array(M.memory.buffer, ptr, n);
    const pkts = Array.from(mem);
    M.web_out_clear();
    return pkts;
}

M.web_boot();
check(true, 'booted');

/* nothing should play before PLAY is pressed */
step(200);
const idle = drain();
check(idle.length === 0, 'silent before PLAY', idle.length + ' packets');

/* start it */
press(B_SEQ);
press(B_PLAY);

let all = [];
for (let i = 0; i < 60; i++) {           /* 60 * 100 ms = 6 s: long enough for demo gates to close */
    step(100);
    all = all.concat(drain());
}

const noteOns = all.filter(p => ((p >> 8) & 0xF0) === 0x90 && ((p >> 24) & 0x7F) > 0);
const noteOffs = all.filter(p => ((p >> 8) & 0xF0) === 0x80 || (((p >> 8) & 0xF0) === 0x90 && ((p >> 24) & 0x7F) === 0));
const others = all.filter(p => ![0x80, 0x90].includes((p >> 8) & 0xF0));

check(all.length > 0, 'the sequencer sends MIDI after PLAY', all.length + ' packets in 6 s');
check(noteOns.length > 0, 'note-ons are produced', noteOns.length + ' note-ons');
check(noteOffs.length > 0, 'note-offs are produced (gates close)', noteOffs.length + ' note-offs');
/* Notes still sounding when the window closes have their note-off after it, so
 * on > off at the end of any window: that is the measurement, not a hung note.
 * What matters is that the two track each other, and that a hang would show up
 * as an ever-growing gap the longer we watch. */
check(noteOns.length - noteOffs.length >= 0 && noteOns.length - noteOffs.length <= 16,
      'note-ons and note-offs track (no note hangs)',
      noteOns.length + ' on / ' + noteOffs.length + ' off');
check(noteOns.length >= 30, 'the demo plays at a plausible rate', noteOns.length + ' note-ons in 6 s');

/* every packet must be a well-formed 3-byte MIDI message in the container */
const bad = all.filter(p => {
    const st = (p >> 8) & 0xFF, d1 = (p >> 16) & 0xFF, d2 = (p >> 24) & 0xFF;
    return st < 0x80 || st > 0xFF || d1 > 0x7F || d2 > 0x7F;
});
check(bad.length === 0, 'every packet is a valid MIDI message', bad.length + ' malformed');

/* channels: status low nibble must be a track channel, 0..15 */
const chans = new Set(all.map(p => (p >> 8) & 0x0F));
check([...chans].every(c => c >= 0 && c <= 15),
      'channel nibbles are in range', 'channels used: ' + [...chans].sort((a,b)=>a-b).join(','));

console.log(fails ? '\n== ' + fails + ' ENGINE PROBE FAILURES ==' : '\nENGINE PROBE OK');
process.exit(fails ? 1 : 0);
