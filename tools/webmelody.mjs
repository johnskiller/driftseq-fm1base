/* melody live-note wire audit on the current web wasm: on/off pairing + auto-gate. */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
const dump = (tag) => {
    const n = M.web_out_n();
    const rows = [];
    for (let i = 0; i < n; i++) {
        const p = M.web_out_pkts(i) >>> 0;
        const st = (p >> 8) & 0xFF, d1 = (p >> 16) & 0xFF, d2 = (p >> 24) & 0xFF;
        rows.push(`${st.toString(16).padStart(2, '0')} ${d1.toString(16).padStart(2, '0')} ${d2.toString(16).padStart(2, '0')}`);
    }
    M.web_out_clear();
    console.log(tag, '→', rows.length, 'pkts:', rows.join(' | '));
};
step(300);
dump('boot(should be empty)');

// 1) live audition, released quickly: expect 90-note-on THEN 80-note-off
M.web_midi_in(0, pkt(0x90, 90, 100));
step(300);
M.web_midi_in(0, pkt(0x80, 90, 0));
step(600);
dump('1) pressed+released note90 (expect on+off)');

// 2) pressed and NEVER released: the 2-cell auto-gate must send the off itself
M.web_midi_in(0, pkt(0x90, 92, 100));
step(1500);
dump('2) held note92, no release (expect on + auto off within ~2 cells)');
