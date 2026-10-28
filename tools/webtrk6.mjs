
import fs from "node:fs";
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync("/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm"),
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
M.web_enc(0, 6);       // SELECT +6 → sel_track 6
peak(50);
// hero screen = M_HOME (default mode at boot, we never navigated) — press a few keys like John:
M.web_keys(1 << 8);
const a1 = peak(300);
const b1 = peak(700);   // auto gate at ~0.5s
const c1 = peak(1500);
M.web_keys(0);
const d1 = peak(300);
console.log(`trk6 hero keys: 0-0.3s ${db(a1)} | 0.3-1.0s ${db(b1)} | 1.0-2.5s ${db(c1)} | after up ${db(d1)}`);
console.log("wire:", dumpWire());
// second note:
M.web_keys(1 << 12);
const a2 = peak(300);
const b2 = peak(1200);
M.web_keys(0);
const d2 = peak(400);
console.log(`trk6 hero 2nd key: 0-0.3s ${db(a2)} | 0.3-1.5s ${db(b2)} | after up ${db(d2)}`);
console.log("wire2:", dumpWire());
