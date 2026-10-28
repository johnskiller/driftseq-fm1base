/* the demo through the web build: PLAY, capture 4s, WAV */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(2800);                        // the page's default, as John heard it
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
step(300);
M.web_buttons(1 << 10); step(30); M.web_buttons(0);   // PLAY
const frames = [];
for (let i = 0; i < Math.round(4.0 * 44100); i += 128) {
    M.web_render(128);
    const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
    const r = new Float32Array(M.memory.buffer, M.web_out_r() | 0, 128);
    for (let k = 0; k < 128; k++) frames.push(l[k], r[k]);
}
const N = frames.length / 2;
const pcm = new Int16Array(frames.length);
for (let i = 0; i < frames.length; i++) pcm[i] = Math.max(-32768, Math.min(32767, Math.round(frames[i] * 32767)));
const hdr = Buffer.alloc(44);
hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + frames.length * 2, 4); hdr.write('WAVEfmt ', 8);
hdr.writeUInt32LE(16, 16); hdr.writeUInt16LE(1, 20); hdr.writeUInt16LE(2, 22);
hdr.writeUInt32LE(44100, 24); hdr.writeUInt32LE(44100 * 4, 28); hdr.writeUInt16LE(4, 32); hdr.writeUInt16LE(16, 34);
hdr.write('data', 36); hdr.writeUInt32LE(frames.length * 2, 40);
fs.writeFileSync('/tmp/webdemo.wav', Buffer.concat([hdr, Buffer.from(pcm.buffer)]));
let clipped = 0, peak = 0;
for (let i = 0; i < frames.length; i++) {
    const v = Math.abs(frames[i]);
    if (v > 0.98) clipped++;
    if (v > peak) peak = v;
}
console.log('peak', peak.toFixed(3), 'clipped samples', clipped);
