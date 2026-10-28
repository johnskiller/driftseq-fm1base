/* AUDIO-level melody sustain check on the current web wasm:
 * piano note held 2s (longer than the 2-cell auto gate) — measure the level
 * after the auto-gate's note-off should have fired (t>2 cells) and long after. */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const T = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
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
T(300);
M.web_midi_in(0, pkt(0x90, 60, 100));        // piano C4 on (sel_track=0 drum ch9 -> raw note)
const p1 = peak(300);                         // sounding
const p2 = peak(1000);                        // auto gate (2 cells DIV_8 @120bpm = 0.5s) fired by now
const p3 = peak(1500);                        // long after
const db = (v) => (20 * Math.log10(v + 1e-9)).toFixed(1);
console.log(`held piano note: peak 0-0.3s ${db(p1)} dBFS | 0.3-1.3s ${db(p2)} dBFS | 1.3-2.8s ${db(p3)} dBFS`);
console.log(p3 < -40 ? 'AUTO-GATE WORKS: note dies well before 3s' : 'STILL SUSTAINING at 2.8s');
