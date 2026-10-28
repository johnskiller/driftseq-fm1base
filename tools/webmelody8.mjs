/* case B timeline: wire packets vs audio level, 100ms slices, 3s. */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const slice = (ms) => {
    const n = Math.round(ms * 44.1);
    let pk = 0;
    const rows = [];
    for (let i = 0; i < n; i += 128) {
        M.web_render(Math.min(128, n - i));
        const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
        for (let k = 0; k < 128; k++) { const a = Math.abs(l[k]); if (a > pk) pk = a; }
    }
    const w = M.web_out_n();
    if (w) {
        const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), w);
        for (let i = 0; i < w; i++) {
            const p = mem[i];
            rows.push(`${((p >>> 8) & 0xff).toString(16)}${((p >>> 16) & 0xff).toString(16).padStart(2, '0')}${((p >>> 24) & 0xff).toString(16).padStart(2, '0')}`);
        }
        M.web_out_clear();
    }
    return { pk, rows };
};
const db = (v) => (20 * Math.log10(v + 1e-9)).toFixed(1);
slice(300);
M.web_midi_in(0, pkt(0x90, 62, 100));
let t = 0.3;
for (let i = 0; i < 30; i++) {
    const { pk, rows } = slice(100);
    t += 0.1;
    console.log(`t=${t.toFixed(1)}s  ${db(pk).padStart(6)} dB  ${rows.join(' | ')}`);
}
