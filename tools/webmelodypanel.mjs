
import fs from "node:fs";
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync("/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm"),
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
const dumpWire = () => {
    const n = M.web_out_n();
    if (!n) return "(empty)";
    const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
    const rows = [];
    for (let i = 0; i < n; i++) { const p = mem[i]; rows.push(`${((p >>> 8) & 0xff).toString(16)} ${((p >>> 16) & 0xff).toString(16)} ${((p >>> 24) & 0xff).toString(16)}`); }
    M.web_out_clear();
    return rows.join(" | ");
};
peak(200);
M.web_enc(0, 3);   // SELECT +3 → sel_track 3 (melody)
peak(100);
M.web_keys(1 << 10);   // press one key (should be a melody-grid cell on trk3)
const a = peak(300);   // while held (auto-gate fires at ~0.5s)
const b = peak(700);   // auto-gate should have off'd by now (0.5s into hold)
const c = peak(1500);  // long after
M.web_keys(0);
const d = peak(400);
console.log(`melody trk3 panel hold: 0-0.3s ${db(a)} | 0.3-1.0s ${db(b)} | 1.0-2.5s ${db(c)} | after up ${db(d)}`);
console.log("wire:", dumpWire());
