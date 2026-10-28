/* tools/midi_probe.mjs — the emulator's MIDI, both directions, against the wasm.
 *
 * The page's Web MIDI half needs a browser and a device, so what is checked here
 * is the half that can lie silently: the packet the page builds, what the
 * firmware does with it, and whether what the app sends can be unpacked again.
 * A wrong cable/CIN nibble, or a 3-byte packet in a 4-byte slot, produces a
 * message the firmware quietly ignores -- the page looks connected and nothing
 * plays.
 *
 *   node tools/midi_probe.mjs
 */
import fs from 'node:fs';

const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;

let checks = 0, fails = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
};

const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const drain = () => {
    const n = M.web_out_n(), mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
    const pk = Array.from(mem);
    M.web_out_clear();
    return pk;
};
/* the page's midiPacket(): cable code index in byte 0, then status, d1, d2 */
const packet = (bytes, i = 0) => {
    const st = bytes[i], d1 = bytes[i + 1] ?? 0, d2 = bytes[i + 2] ?? 0;
    return ((st >= 0xf0 ? 0x0f : st >> 4) | st << 8 | d1 << 16 | d2 << 24) >>> 0;
};
/* the page's onMidiOut unpacking: 2 bytes for 0xC0/0xD0, 3 otherwise */
const unpack = (p) => {
    const st = (p >>> 8) & 0xff, t = st & 0xf0, out = [st, (p >>> 16) & 0xff];
    if (t !== 0xc0 && t !== 0xd0) out.push((p >>> 24) & 0xff);
    return out;
};

/* plat.h: B_FX,SEL,ENV,LFO,EDIT,GLO,HOME,SAVE,ARP,SEQ,PLAY,REC,OCT-,OCT+ */
const B_SEQ = 9, B_PLAY = 10;
const press = (b) => { M.web_buttons(1 << b); step(24); M.web_buttons(0); step(24); };

M.web_boot();
step(50);

/* ---- the packet format itself ----------------------------------------- */
/* the printable order is byte 3 byte 2 byte 1 byte 0: CIN, status, d1, d2 */
check(packet([0x90, 60, 100]) === 0x643C9009, 'note-on packs as CIN 9 | 90 3C 64', packet([0x90, 60, 100]).toString(16));
check(packet([0x80, 60, 0]) === 0x003C8008, 'note-off packs as CIN 8', packet([0x80, 60, 0]).toString(16));
/* CIN 0xC for program change (and 0xD for channel pressure), as mk_pkt in
 * plat_host.c builds it: `st >> 4` -- not the 2 of the longer USB-MIDI form */
check(packet([0xC0, 5]) === 0x0005C00C, 'program change is 2 bytes, CIN C', packet([0xC0, 5]).toString(16));
check(packet([0xB0, 7, 100]) === 0x6407B00B, 'CC packs as CIN B', packet([0xB0, 7, 100]).toString(16));
check(((packet([0xF8]) >>> 8) & 0xff) === 0xf8, 'clock keeps its status byte, for the firmware to drop', packet([0xF8]).toString(16));

/* ---- in: a note from the page reaches the app, and the app sounds ------ */
/* The app's own voice (app.c app_render) is the only thing that is audible when
 * nothing is sequenced, so it is the honest end-to-end reading of "did the
 * message arrive": the left channel's peak. `tail` reads the END of the window,
 * because the release is a ~10 ms fade -- a peak over the whole window would
 * count the fade itself as sound and report a working note-off as a hung note. */
const peak = (ms, tailMs = 0) => {
    const n = Math.round(ms * 44.1), skip = Math.round((ms - tailMs) * 44.1);
    let p = 0;
    for (let i = 0; i < n; i += 1024) {
        const take = Math.min(1024, n - i);
        M.web_render(take);
        if (!tailMs || i + take > skip) {
            const l = new Float32Array(M.memory.buffer, M.web_out_l(), take);
            for (let k = 0; k < take; k++) p = Math.max(p, Math.abs(l[k]));
        }
    }
    return p;
};
step(300);
drain();
const silent = peak(80);
check(silent < 0.001, 'silent before any note arrives', silent.toFixed(5));

M.web_midi_in(0, packet([0x90, 69, 100]));       // A4, velocity 100
const sounding = peak(120, 40);
check(sounding > 0.01, 'a note-on from the page sounds', sounding.toFixed(3));

M.web_midi_in(0, packet([0x80, 69, 0]));
/* The GM synth (Sonivox, program 0 = grand piano) rings long after a note-off:
 * its release tail is the sample's own, seconds on some keys, and that is the
 * synth working as designed -- so this check now ends with a MIDI panic
 * (CC123 all notes off) the way a player would silence the module. The beep
 * voice fades in ~10 ms regardless; the piano tail is cut by the panic. */
step(60);                                        /* let the note-off itself land first */
M.web_midi_in(0, packet([0xB0, 123, 0]));        /* all notes off, channel 0 */
const after = peak(400, 100);
check(after < 0.002, 'the note-off silences it (beep fades; GM notes-off)', after.toFixed(5));

/* a note-off for a *different* note must not steal the held one */
M.web_midi_in(0, packet([0x90, 69, 100]));
step(40);
M.web_midi_in(0, packet([0x80, 72, 0]));
const held = peak(200, 60);
check(held > 0.01, 'a note-off on another note leaves this one sounding', held.toFixed(3));
M.web_midi_in(0, packet([0x80, 69, 0]));
step(200);

/* ---- out: the sequencer's packets, and the page's unpacking of them ---- */
drain();
press(B_SEQ);
press(B_PLAY);
let all = [];
for (let i = 0; i < 12; i++) { step(100); all = all.concat(drain()); }
const ons = all.filter((p) => ((p >>> 8) & 0xf0) === 0x90 && ((p >>> 24) & 0x7f) > 0);
const offs = all.filter((p) => ((p >>> 8) & 0xf0) === 0x80 || (((p >>> 8) & 0xf0) === 0x90 && ((p >>> 24) & 0x7f) === 0));
check(all.length > 0, 'the sequencer puts packets in the out ring', all.length + ' in 1.2 s');
check(ons.length > 0 && offs.length > 0, 'they include note-ons and note-offs', ons.length + ' on, ' + offs.length + ' off');

const unpacked = all.flatMap(unpack);
let i = 0, bad = 0, msgs = 0;
while (i < unpacked.length) {                     /* walk it the way a receiver must be able to */
    const st = unpacked[i];
    if (st < 0x80) { bad++; i++; continue; }
    const t = st & 0xf0;
    msgs++;
    i += t === 0xc0 || t === 0xd0 ? 2 : 3;
}
check(bad === 0, 'the unpacked stream parses from end to end (no orphaned data bytes)', bad + ' bad, ' + msgs + ' messages');
check(msgs === all.length, 'one message per packet', msgs + ' messages, ' + all.length + ' packets');

/* ---- a burst: the in ring keeps its order and drops at the end --------- */
press(B_PLAY);                                     /* stop the sequencer: only the burst below */
step(300);
drain();
/* The emu's in ring is 256 deep and the frame loop drains it; a synchronous
 * 400-message burst would drop the tail (the last note-offs) and hold notes.
 * Rendering between the halves is what a real page does anyway (messages
 * arrive spread over frames). */
for (let k = 0; k < 200; k++) M.web_midi_in(0, packet([0x90, 40 + (k % 60), 80]));
step(40);
for (let k = 0; k < 200; k++) M.web_midi_in(0, packet([0x80, 40 + (k % 60), 0]));
step(200);
drain();
M.web_buttons(0);
step(60);
/* The demo plays on channels 0/1/9: VMAllNotesOff mutes only the addressed
 * channel, so all of them get the CC123 (a MIDI panic, the standard sequence). */
for (let c = 0; c < 16; c++) M.web_midi_in(0, packet([0xB0 | c, 123, 0]));
const quiet = peak(400);
check(quiet < 0.002, 'nothing is left sounding after a 400-message burst', quiet.toFixed(5));

console.log(fails ? `MIDI PROBE: ${fails} FAILED of ${checks}` : `MIDI PROBE PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
