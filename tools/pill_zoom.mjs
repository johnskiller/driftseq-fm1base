/* tools/pill_zoom.mjs — the pill at 8x, single digit and double digit.
 *
 * The corner is the whole point: a chamfer drawn as one pixel per row shows up
 * here as a staircase with gaps, and a real arc shows up as insets of
 * 2,1,0,0 / body / 0,0,1,2. Prints those insets as numbers too, so the shape can
 * be checked without trusting an eye or a vision model. */
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
const rgb = (c0) => { const c = swap(c0); const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

/* Measure the corner: for each row, how far in does the pill start? */
function insets(fb, x0, y0, w, h) {
    const bg = rgb(fb[(y0 - 2) * W + 120]);
    const isBg = (c) => Math.abs(c[0] - bg[0]) + Math.abs(c[1] - bg[1]) + Math.abs(c[2] - bg[2]) < 14;
    const rows = [];
    for (let y = 0; y < h; y++) {
        let l = -1, r = -1;
        for (let x = 0; x < w; x++) if (!isBg(rgb(fb[(y0 + y) * W + x0 + x]))) { if (l < 0) l = x; r = x; }
        rows.push(l < 0 ? null : { l, r });
    }
    return rows;
}

const shots = [];
for (const n of [1, 10]) {
    for (let i = 0; i < 32; i++) M.web_enc(0, -1);
    step(20);
    for (let i = 0; i < n - 1; i++) M.web_enc(0, 1);
    step(60);
    const fb = grab();
    const ins = insets(fb, 6, 3, 24, 18);
    const left = ins.map((v) => (v ? v.l : -1));
    console.log(`track ${n}: left inset per row (y3..y20) = ${left.join(',')}`);
    shots.push({ n, fb });
}

const SCALE = 8, CW = 40, CH = 24, GAP = 8;
const OW = CW * SCALE, OH = shots.length * (CH * SCALE + GAP);
const px = new Uint8Array(OW * OH * 3).fill(16);
const put = (x, y, r, g, b) => {
    if (x < 0 || y < 0 || x >= OW || y >= OH) return;
    const o = (y * OW + x) * 3; px[o] = r; px[o + 1] = g; px[o + 2] = b;
};
shots.forEach((s, si) => {
    const oy = si * (CH * SCALE + GAP);
    for (let y = 0; y < CH; y++) for (let x = 0; x < CW; x++) {
        const [r, g, b] = rgb(s.fb[y * W + x]);
        for (let dy = 0; dy < SCALE; dy++) for (let dx = 0; dx < SCALE; dx++)
            put(x * SCALE + dx, oy + y * SCALE + dy, r, g, b);
    }
});

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
ihdr[8] = 8; ihdr[9] = 2;
const raw = Buffer.alloc(OH * (OW * 3 + 1));
for (let y = 0; y < OH; y++) {
    raw[y * (OW * 3 + 1)] = 0;
    Buffer.from(px.buffer, y * OW * 3, OW * 3).copy(raw, y * (OW * 3 + 1) + 1);
}
const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
]);
fs.writeFileSync('/home/john/.hermes/workspace/driftseq-fm1base/docs/pill-zoom.png', png);
console.log('wrote docs/pill-zoom.png', `${OW}x${OH}`, png.length, 'B', '— top: track 1, bottom: track 10');
