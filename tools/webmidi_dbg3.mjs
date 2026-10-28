/* is EAS rendering on the web build at all? capture 3s around a kick honestly this time */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
step(300);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
// read out_l DIRECTLY per quantum instead of my broken ring copy:
const dump = [];
M.web_midi_in(0, pkt(0x99, 24, 0x7f));
let peak = 0;
for (let i = 0; i < Math.round(1.5 * 44100); i += 128) {
    M.web_render(128);
    const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
    for (let k = 0; k < 128; k++) peak = Math.max(peak, Math.abs(l[k]));
    if (i % 4410 === 0) dump.push(((i / 44100) + 0.3).toFixed(2) + 's:' + l[0].toFixed(3));
}
console.log('peak over 1.5s after kick:', peak.toFixed(4));
console.log(dump.join(' '));
