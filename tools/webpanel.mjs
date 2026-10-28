/* PANEL-KEY melody note test (what John actually does: click a grid cell):
 * web_keys(bitmask) down/up on a MELODY track cell, measure audio after auto-gate. */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const peak = (ms) => {
    const n = Math.round(ms * 44.1);
    let pk = 0;
    for (let i = 0; i < n; i += 128) {
        M.web_render(Math.min(128, n - i));
        const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
        for (let k = 0; k < 128; k++) { const a = Math.abs(l[k]); if (a > pk) pk = a; }
    }
    return pk;
};
const db = (v) => (20 * Math.log10(v + 1e-9)).toFixed(1);

// Which key index is a melody-grid cell? First find how web_keys maps: try key 0 = first panel key.
// On boot sel_track=0 (drum). Switch to a melody track first: emulate the UI track-select.
// Simpler: use web_midi_in program-flow? No — PANEL path. Try keys bit 0 (w0): on the drum page
// that's the kick lane. We need a MELODY track selected. The UI selects tracks with buttons;
// web_buttons(bitmask). Which button = track right? Unknown; probe UI docs in index.html keymap.
// Instead: inject a MIDI note-in on ch0 out of drum range to enter step-entry on track 0? No.
// Fastest: call the engine's live API through the SAME handler the grid uses by pressing keys:
// grid melody cells only exist on melody pages. Use web_buttons to navigate: try every single
// button bit, render a frame, and detect page change via web_said or screen is hard.
// ALTERNATIVE: trust the earlier finding — panel melody path calls the SAME ds_live_on/off with
// auto-gate off going through engine_drain → gm_off(sel_ch). The probe below presses a key and
// reads the WIRE to see if the auto-gate off carries the right channel:
peak(200);
M.web_keys(0xffffffff);           // all keys down (any grid cell on the default page)
const a = peak(300);
M.web_keys(0);                    // all up
const b = peak(500);
console.log(`panel all-keys tap: hold ${db(a)} | after up ${db(b)}`);
const n = M.web_out_n();
const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
const rows = [];
for (let i = 0; i < n; i++) { const p = mem[i]; rows.push(`${((p >>> 8) & 0xff).toString(16)} ${((p >>> 16) & 0xff).toString(16)} ${((p >>> 24) & 0xff).toString(16)}`); }
M.web_out_clear();
console.log('wire:', rows.slice(0, 20).join(' | '));
const ons = rows.filter(r => r.startsWith('9')).length;
const offs = rows.filter(r => r.startsWith('8')).length;
console.log(`on=${ons} off=${offs}`);
// melody-specific: find a melody cell by pressing ONLY one key that is a white key on the
// bottom rows (melody grid region). Try single key bits one at a time and record which emit
// melodic (ch0/1) notes:
M.web_out_clear();
for (let k = 0; k < 40; k++) {
    M.web_keys(1 << k);
    peak(60);
    M.web_keys(0);
    peak(40);
}
const n2 = M.web_out_n();
const mem2 = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n2);
const per = {};
for (let i = 0; i < n2; i++) { const p = mem2[i]; const st = (p >>> 8) & 0xff; const key = `${st.toString(16)}`; per[key] = (per[key] || 0) + 1; }
console.log('status bytes over single-key scan:', JSON.stringify(per));
