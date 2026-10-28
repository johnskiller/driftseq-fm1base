/* web-side GM: single kick, capture everything, WAV + envelope */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
step(300);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const SR = 44100, N = SR * 3;
const L = new Float32Array(N), R = new Float32Array(N);
let wi = 0;
const capture = (ms) => {
    const n = Math.min(Math.round(ms * 44.1), N);
    for (let i = 0; i < n; i += 128) {
        M.web_render(128);
        const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
        const r = new Float32Array(M.memory.buffer, M.web_out_r() | 0, 128);
        for (let k = 0; k < 128 && wi + k < n; k++) { L[wi + k] = l[k]; R[wi + k] = r[k]; }
    }
};
capture(100);
M.web_midi_in(0, pkt(0x99, 24, 0x7f));
capture(900);
M.web_midi_in(0, pkt(0x89, 24, 0));
capture(600);
M.web_midi_in(0, pkt(0x90, 60, 0x64));
capture(200);
M.web_midi_in(0, pkt(0x80, 60, 0));
capture(1300);
let peak = 0;
for (let i = 0; i < N; i++) peak = Math.max(peak, Math.abs(L[i]));
const win = 441;
const marks = [];
for (let i = 0; i < N - win; i += win) {
    let mx = 0;
    for (let k = 0; k < win; k++) mx = Math.max(mx, Math.abs(L[i + k]));
    if (mx > 0.002) marks.push(`${(i / SR).toFixed(2)}s:${(20 * Math.log10(mx)).toFixed(0)}dB`);
}
console.log(marks.join(' '));
console.log('peak L:', peak.toFixed(3));
