/* isolate the sustaining voice: beep vs EAS.
 * after the hold + auto-gate, send explicit 0x80 62 (note_off + gm_off ch0). */
import fs from "node:fs";
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync("/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm"),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot(); M.web_master(4096);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const peak = (ms) => { const n = Math.round(ms * 44.1); let pk = 0;
  for (let i = 0; i < n; i += 128) { M.web_render(Math.min(128, n - i));
    const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
    for (let k = 0; k < 128; k++) { const a = Math.abs(l[k]); if (a > pk) pk = a; } } return pk; };
const db = (v) => (20 * Math.log10(v + 1e-9)).toFixed(1);
peak(300);
M.web_midi_in(0, pkt(0x90, 62, 100));
peak(1000);
console.log("sustaining (auto-gate fired):", db(peak(200)), "dBFS");
M.web_midi_in(0, pkt(0x80, 62, 0));          // explicit note-off (any ch): beep + EAS ch0
console.log("after explicit 80 62    :", db(peak(100)), "dBFS");
peak(400);
console.log("400ms later             :", db(peak(100)), "dBFS");
