/* Is the sustained audio the EAS or the app's beep preview? Compare:
 * A) explicit off after 300ms — expect release to silence
 * B) held with auto-gate — the observed slow decay */
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
        const r = new Float32Array(M.memory.buffer, M.web_out_r() | 0, 128);
        for (let k = 0; k < 128; k++) { const a = Math.abs(l[k]) + Math.abs(r[k]); if (a > pk) pk = a; }
    }
    return pk / 2;
};
const db = (v) => (20 * Math.log10(v + 1e-9)).toFixed(1);

T(300);
console.log('--- A: explicit off after 300ms ---');
M.web_midi_in(0, pkt(0x90, 60, 100));
console.log('  on (0-0.3s):', db(peak(300)));
M.web_midi_in(0, pkt(0x80, 60, 0));
console.log('  off->0.1s:', db(peak(100)));
console.log('  off->0.5s:', db(peak(400)));
console.log('  off->1.5s:', db(peak(1000)));

console.log('--- B: held, no explicit off (auto gate should fire ~0.5s) ---');
M.web_midi_in(0, pkt(0x90, 62, 100));
console.log('  on (0-0.3s):', db(peak(300)));
console.log('  0.3-0.8s:', db(peak(500)));
console.log('  0.8-1.5s:', db(peak(700)));
console.log('  1.5-3.0s:', db(peak(1500)));
