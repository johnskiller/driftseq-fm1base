/* when do the mystery ch10 note-5 packets appear? fine-grained timeline. */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const drain = () => {
    const n = M.web_out_n();
    const rows = [];
    for (let i = 0; i < n; i++) {
        const p = M.web_out_pkts(i) >>> 0;
        rows.push(`${((p >> 8) & 0xFF).toString(16).padStart(2, '0')} ${((p >> 16) & 0xFF).toString(16).padStart(2, '0')} ${((p >> 24) & 0xFF).toString(16).padStart(2, '0')}`);
    }
    M.web_out_clear();
    return rows;
};
const T = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };

T(300); console.log('t=0.30s boot:', drain().join('|') || '(empty)');
M.web_midi_in(0, pkt(0x90, 90, 100));
for (let t = 0; t < 30; t++) {          // 100ms slices for 3s
    T(100);
    const d = drain();
    if (d.length) console.log(`t=${(0.3 + (t + 1) * 0.1).toFixed(1)}s:`, d.join('|'));
}
