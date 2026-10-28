/* verify the melody-sustain fix: MIDI-in note held, auto-gate off must silence BOTH
 * EAS and (former) beep. Expect: sustain gone within ~0.5s+release. */
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
peak(300);
M.web_midi_in(0, pkt(0x90, 62, 100));
const a = peak(300);
const b = peak(1000);   // auto gate (2 cells) fired ~0.5s in
const c = peak(1500);
console.log(`held MIDI-in note: 0-0.3s ${db(a)} | 0.3-1.3s ${db(b)} | 1.3-2.8s ${db(c)}`);
console.log(b < -40 && c < -45 ? 'FIXED: dies after the auto gate (EAS release)' : 'STILL SUSTAINING');
// wire pairing check
M.web_midi_in(0, pkt(0x90, 65, 100));
peak(300);
const n = M.web_out_n();
const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
const rows = [];
for (let i = 0; i < n; i++) { const p = mem[i]; rows.push(`${((p >>> 8) & 0xff).toString(16)} ${((p >>> 16) & 0xff).toString(16)} ${((p >>> 24) & 0xff).toString(16)}`); }
M.web_out_clear();
console.log('wire after 300ms held:', rows.join(' | '));
