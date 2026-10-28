import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const T = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
// press PLAY: the panel PLAY button = which input? web_keys bitmap. From the shortcuts: PLAY is a panel button.
// Easier: send MIDI start FA? ui_midi_realtime: FA = play.
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
T(300);
const drain = (tag) => {
    const n = M.web_out_n();
    if (!n) return;
    const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
    const rows = [];
    for (let i = 0; i < n; i++) {
        const p = mem[i];
        rows.push(`${((p >>> 8) & 0xff).toString(16).padStart(2, '0')} ${((p >>> 16) & 0xff).toString(16).padStart(2, '0')} ${((p >>> 24) & 0xff).toString(16).padStart(2, '0')}`);
    }
    M.web_out_clear();
    console.log(tag, rows.join(' | '));
};
M.web_midi_in(0, pkt(0xFA, 0, 0));      // MIDI start
let ons = 0, offs = 0;
for (let s = 0; s < 80; s++) {           // 4 s, 100ms slices
    T(100);
    const n = M.web_out_n();
    if (n) {
        const mem = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
        for (let i = 0; i < n; i++) {
            const p = mem[i], st = (p >>> 8) & 0xff;
            if (st === 0x90 || st === 0x91) ons++;
            if (st === 0x80 || st === 0x81) offs++;
        }
        M.web_out_clear();
    }
}
console.log(`web demo 8s: melody(ch0/1) on=${ons} off=${offs}`);
