import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
step(300);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
M.web_midi_in(0, pkt(0x99, 24, 0x7f));
step(200);
const n = M.web_out_n(), mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
console.log('out packets:', n);
for (let i = 0; i < Math.min(n, 8); i++) {
    const p = mem[i];
    console.log('  ', ((p >>> 8) & 0xff).toString(16), (p >>> 16) & 0xff, (p >>> 24) & 0xff);
}
