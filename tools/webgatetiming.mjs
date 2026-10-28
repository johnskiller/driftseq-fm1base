
import fs from "node:fs";
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync("/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm"),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const t0 = Date.now();
const seen = [];
const poll = () => {
    const n = M.web_out_n();
    if (n) {
        const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
        for (let i = 0; i < n; i++) {
            const p = mem[i];
            seen.push(`t=${((Date.now()-t0)/1000).toFixed(2)}s ${((p >>> 8) & 0xff).toString(16)} ${((p >>> 16) & 0xff).toString(16)} ${((p >>> 24) & 0xff).toString(16)}`);
        }
        M.web_out_clear();
    }
};
// render ~6s, hold the key 3s then release
M.web_render(128);
M.web_enc(0, 6);
M.web_keys(1 << 10);                    // down at t≈0
for (let i = 0; i < Math.round(3.0 * 44.1 * 1024 / 128); i++) { M.web_render(128); poll(); }
M.web_keys(0);                          // up at t=3.0s
for (let i = 0; i < Math.round(0.5 * 44.1 * 1024 / 128); i++) { M.web_render(128); poll(); }
console.log(seen.join("\n"));
