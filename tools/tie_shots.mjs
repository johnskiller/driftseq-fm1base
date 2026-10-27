/* tools/tie_shots.mjs — the three states side by side, as a PNG.
 *
 * A probe proves the geometry; this is for the eyes. The crop is the first two
 * bars of the melodic GRID (y 16..112), scaled 4x, stacked: play / tie / rest.
 * A tied run should read as ONE bar running across both cells, an attack should
 * sit inside its cell with air on both sides, and a rest should be absent.
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();

const B_GLO = 5, B_SEQ = 9, B_OCTUP = 13;
const EN_SELECT = 0, EN_K1 = 3;

const exp = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

M.web_boot(); step(400);
M.web_buttons(1 << B_SEQ); step(80); M.web_buttons(0); step(150);
M.web_enc(EN_SELECT, 6); step(120);

let cursor = 0;
const cursorTo = (s) => { if (s !== cursor) { M.web_enc(EN_K1, s - cursor); cursor = s; step(60); } };
const key = (k, ms = 70) => { M.web_keys(1 << k); step(ms); M.web_keys(0); step(ms); };
const cycle = () => {
    M.web_buttons(1 << B_GLO); step(60);
    M.web_buttons((1 << B_GLO) | (1 << B_OCTUP)); step(60);
    M.web_buttons(1 << B_GLO); step(60);
    M.web_buttons(0); step(80);
};

/* a legato phrase: C, then three steps tied, then G -- so the tie run is visible
 * as one continuous bar rather than a single seam */
for (const k of [0, 4, 7, 11]) { key(k); cursor++; }
const A = 0;
cursorTo(A + 1); cycle();                      /* tie step 1 */
cursorTo(A + 2); cycle();                      /* tie step 2 */
cursorTo(A + 3); cycle();                      /* tie step 3 */

const CROP_Y = 12, CROP_H = 106, SCALE = 4;
const shots = [];
const PARK = 12;

const shot = (label) => { cursorTo(PARK); shots.push({ label, fb: grab() }); };

shot('1. play: C attacked on the left, G on the right');
cursorTo(A + 1); cycle();                      /* 1: tie -> rest */
shot('2. step 1 is a rest: the bar breaks');
cursorTo(A + 2); cycle();                      /* 2: tie -> rest  */
cursorTo(A + 3); cycle();                      /* 3: tie -> rest  */
shot('3. steps 1-3 all rest: only the attacks remain');

const OW = W * SCALE, GAP = 6 * SCALE;
const OH = shots.length * (CROP_H * SCALE + GAP);
const px = new Uint8Array(OW * OH * 3);
const put = (x, y, r, g, b) => {
    if (x < 0 || y < 0 || x >= OW || y >= OH) return;
    const o = (y * OW + x) * 3; px[o] = r; px[o + 1] = g; px[o + 2] = b;
};
shots.forEach((s, si) => {
    const oy = si * (CROP_H * SCALE + GAP);
    for (let y = 0; y < CROP_H; y++) for (let x = 0; x < W; x++) {
        const [r, g, b] = exp(s.fb[(CROP_Y + y) * W + x]);
        for (let dy = 0; dy < SCALE; dy++) for (let dx = 0; dx < SCALE; dx++)
            put(x * SCALE + dx, oy + y * SCALE + dy, r, g, b);
    }
    /* mark the cell boundaries of the 4 steps under test with a thin rule at the
     * very top, so "reaches past the edge" is measurable by eye */
    for (let k = 0; k <= 4; k++) {
        const x = (18 + k * 13) * SCALE;
        for (let y = 0; y < 3 * SCALE; y++) put(x, oy + y, 90, 90, 110);
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
const out = '/home/john/.hermes/workspace/driftseq-fm1base/docs/tie-states.png';
fs.writeFileSync(out, png);
console.log(`wrote ${out}  ${OW}x${OH}  ${png.length} B`);
console.log('rows top to bottom:');
for (const s of shots) console.log('  ' + s.label);
