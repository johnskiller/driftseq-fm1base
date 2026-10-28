/* full web-side capture: boot, kick, piano, into a WAV; envelope + spectrum */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(4096);
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 128) M.web_render(Math.min(128, n - i)); };
step(300);
const pkt = (st, d1, d2) => ((st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
const SR = 44100;
const frames = [];                                   // {t, l}
let t = 0.3;
const grab = (ms) => {
    const n = Math.round(ms * 44.1);
    for (let i = 0; i < n; i += 128) {
        M.web_render(128);
        const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
        const r = new Float32Array(M.memory.buffer, M.web_out_r() | 0, 128);
        for (let k = 0; k < 128; k++) frames.push(l[k], r[k]);
    }
};
grab(50);
M.web_midi_in(0, pkt(0x99, 24, 0x7f));               // kick
grab(900);
M.web_midi_in(0, pkt(0x89, 24, 0));                  // kick off
grab(400);
M.web_midi_in(0, pkt(0x90, 60, 0x64));               // piano
grab(150);
M.web_midi_in(0, pkt(0x80, 60, 0));                  // piano off -> release
grab(1500);

const N = frames.length / 2;
const hdr = Buffer.alloc(44);
hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + frames.length * 2, 4); hdr.write('WAVEfmt ', 8);
hdr.writeUInt32LE(16, 16); hdr.writeUInt16LE(1, 20); hdr.writeUInt16LE(2, 22);
hdr.writeUInt32LE(SR, 24); hdr.writeUInt32LE(SR * 4, 28); hdr.writeUInt16LE(4, 32); hdr.writeUInt16LE(16, 34);
hdr.write('data', 36); hdr.writeUInt32LE(frames.length * 2, 40);
const pcm = new Int16Array(frames.length);
for (let i = 0; i < frames.length; i++) pcm[i] = Math.max(-32768, Math.min(32767, Math.round(frames[i] * 32767)));
fs.writeFileSync('/tmp/webkick.wav', Buffer.concat([hdr, Buffer.from(pcm.buffer)]));

// envelope 10ms
const win = 441;
const marks = [];
for (let i = 0; i < N - win; i += win) {
    let mx = 0;
    for (let k = 0; k < win; k++) mx = Math.max(mx, Math.abs(frames[2 * (i + k)]));
    if (mx > 0.003) marks.push(`${(i / SR).toFixed(2)}:${(20 * Math.log10(mx)).toFixed(0)}dB`);
}
console.log(marks.join(' '));
// spectrum of the piano tail (last 0.3s of the capture)
const x0 = N - Math.round(0.3 * SR);
const x = [];
for (let i = 0; i < 4096 && x0 + i < N; i++) x.push(frames[2 * (x0 + i)]);
const spec = (f) => { let re = 0, im = 0; for (let i = 0; i < x.length; i++) { re += x[i] * Math.cos(2 * Math.PI * f * i / SR); im -= x[i] * Math.sin(2 * Math.PI * f * i / SR); } return Math.hypot(re, im) / x.length; };
console.log('tail spectrum:', [262, 524, 1046, 4000, 8000, 11025, 12000, 16000, 20000].map(f => `${f}:${spec(f).toFixed(4)}`).join(' '));
