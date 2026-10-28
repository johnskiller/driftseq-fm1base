/* Option A verification on web wasm: hold key 2s (device-time) → no off during hold;
 * key-up → off. Audio: sustains while held, dies after up. */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
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
const wire = () => {
    const n = M.web_out_n();
    if (!n) return [];
    const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
    const rows = [];
    for (let i = 0; i < n; i++) { const p = mem[i]; rows.push(`${((p >>> 8) & 0xff).toString(16)} ${((p >>> 16) & 0xff).toString(16)} ${((p >>> 24) & 0xff).toString(16)}`); }
    M.web_out_clear();
    return rows;
};
peak(200);
M.web_enc(0, 6);
M.web_keys(1 << 10);
const a = peak(400);      // 0.4s into hold — old gate would have off'd at 0.25s
const w1 = wire();
const b = peak(800);      // 1.2s into hold
const w2 = wire();
const c = peak(800);      // 2.0s into hold
const w3 = wire();
M.web_keys(0);            // up
const w4 = wire();
const d = peak(400);
const w5 = wire();
console.log(`hold: 0-0.4s ${db(a)} | 0.4-1.2s ${db(b)} | 1.2-2.0s ${db(c)} | after up ${db(d)}`);
console.log(`wire during hold: ${[...w1, ...w2, ...w3].join(' | ') || '(only the on, no off)'}`);
console.log(`wire after up: ${w4.join(' | ')} then ${w5.join(' | ') || '(nothing)'}`);
