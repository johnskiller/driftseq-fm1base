/* tools/pill_shots.mjs — track 9 next to track 10, cropped to the header.
 *
 * John: "track 1-9和10-16药丸大小要保持一致，避免header跳动". This renders both
 * and stacks them so the pill's right edge and the context text's left edge can
 * be compared by eye, with a vertical rule at the shared x. */
import fs from 'node:fs';
import zlib from 'node:zlib';

const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();
M.web_boot(); step(300);

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const exp = (c0) => { const c = swap(c0); const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

/* select track n (1..16) by walking the selection with K1 = START? no: the row
 * cursor is moved with the encoder while HOME is focused on the track list, so
 * use the same call the UI exposes: web_enc(0, +1) walks sel_track. */
const selectTrack = (n) => {
    for (let i = 0; i < 32; i++) M.web_enc(0, -1);       /* back to track 1 */
    step(20);
    for (let i = 0; i < n - 1; i++) M.web_enc(0, 1);
    step(60);
};

const shots = [];
for (const n of [1, 9, 10, 16]) {
    selectTrack(n);
    shots.push({ n, fb: grab() });
}
console.log('captured tracks', shots.map((s) => s.n).join(', '));

/* measure the pill's right edge and the first text ink after it.
 *
 * Only look left of x=34 for the pill: the earlier version scanned to x=80 and
 * happily reported the body text's own extent, which is why every track came
 * back with the same nonsense number. */
for (const s of shots) {
    const bg = exp(s.fb[5 * W + 200]);
    const isBg = (c) => Math.abs(c[0] - bg[0]) + Math.abs(c[1] - bg[1]) + Math.abs(c[2] - bg[2]) < 30;
    let pillR = -1, pillL = -1;
    for (let x = 0; x < 34; x++) {
        let solid = 0;
        for (let y = 4; y < 20; y++) if (!isBg(exp(s.fb[y * W + x]))) solid++;
        if (solid >= 12) { if (pillL < 0) pillL = x; pillR = x; }
    }
    let ctxX = -1;
    for (let x = pillR + 1; x < 200; x++) {
        let ink = 0;
        for (let y = 4; y < 20; y++) { const c = exp(s.fb[y * W + x]); if ((c[0] + c[1] + c[2]) / 3 > 150) ink++; }
        if (ink >= 2) { ctxX = x; break; }
    }
    console.log(`  track ${String(s.n).padStart(2)}: pill x=${pillL}..${pillR} (w=${pillR - pillL + 1}), text starts x=${ctxX}`);
}

/* compose: 4 crops of y0..24 stacked, 3x, with a rule at the shared x */
const SCALE = 3, CROP_H = 26, GAP = 6;
const OW = W * SCALE, OH = shots.length * (CROP_H * SCALE + GAP);
const px = new Uint8Array(OW * OH * 3).fill(18);
const put = (x, y, r, g, b) => {
    if (x < 0 || y < 0 || x >= OW || y >= OH) return;
    const o = (y * OW + x) * 3; px[o] = r; px[o + 1] = g; px[o + 2] = b;
};
shots.forEach((s, si) => {
    const oy = si * (CROP_H * SCALE + GAP);
    for (let y = 0; y < CROP_H; y++) for (let x = 0; x < W; x++) {
        const [r, g, b] = exp(s.fb[y * W + x]);
        for (let dy = 0; dy < SCALE; dy++) for (let dx = 0; dx < SCALE; dx++)
            put(x * SCALE + dx, oy + y * SCALE + dy, r, g, b);
    }
});
/* vertical rule at x=30*SCALE (the pill's right edge) to make any shift obvious */
for (let si = 0; si < shots.length; si++) {
    const oy = si * (CROP_H * SCALE + GAP);
    for (let y = 0; y < CROP_H * SCALE; y++) put(30 * SCALE, oy + y, 255, 90, 40);
}

/* PNG encode */
const crcT = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc = (b) => { let c = -1; for (const v of b) c = crcT[(c ^ v) & 0xff] ^ (c >>> 8); return (c ^ -1) >>> 0; };
const chunk = (t, d) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(d.length);
    const td = Buffer.concat([Buffer.from(t, 'ascii'), d]);
    const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
    return Buffer.concat([len, td, c]);
};
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(OW, 0); ihdr.writeUInt32BE(OH, 4);
ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
const raw = Buffer.alloc(OH * (OW * 3 + 1));
for (let y = 0; y < OH; y++) {
    raw[y * (OW * 3 + 1)] = 0;
    Buffer.from(px.buffer, y * OW * 3, OW * 3).copy(raw, y * (OW * 3 + 1) + 1);
}
const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
]);
fs.writeFileSync('/home/john/.hermes/workspace/driftseq-fm1base/docs/pill-stable.png', png);
console.log('wrote docs/pill-stable.png', `${OW}x${OH}`, png.length, 'B');
console.log('rows, top to bottom: track 1, 9, 10, 16 — red rule marks the pill right edge');
