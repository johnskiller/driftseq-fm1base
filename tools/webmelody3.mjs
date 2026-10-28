/* melody live-note wire audit, correct out-pkt reader. */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const T = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
const drain = (tag) => {
    const n = M.web_out_n();
    if (!n) { if (tag) console.log(tag, '(empty)'); return; }
    const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
    const rows = [];
    for (let i = 0; i < n; i++) {
        const p = mem[i];
        rows.push(`${((p >>> 8) & 0xff).toString(16).padStart(2, '0')} ${((p >>> 16) & 0xff).toString(16).padStart(2, '0')} ${((p >>> 24) & 0xff).toString(16).padStart(2, '0')}`);
    }
    M.web_out_clear();
    console.log(tag || 'out', rows.length, "pkts:", rows.join(" | "));
};
T(300); drain("boot:");
// 1) melody audition, released
M.web_midi_in(0, pkt(0x90, 90, 100));
T(300);
M.web_midi_in(0, pkt(0x80, 90, 0));
T(700);
drain("1) note90 pressed+released:");
// 2) pressed, NEVER released: auto gate must fire
M.web_midi_in(0, pkt(0x90, 92, 100));
T(1500);
drain("2) note92 held, no release:");
