/* tools/pill_radius.mjs — candidate radii for the pill, side by side.
 *
 * Parses RR_INSET straight out of canvas_extra.h so this cannot drift from what
 * the firmware actually draws, then paints each radius with the same row-fill
 * algorithm cv_round_rect uses. Output is 6x so the arc's steps are visible.
 *
 * Also reports, for each radius, whether the corner would clip the digits: the
 * digits are knocked out in the background colour starting at x=5 inside the
 * pill, so an inset above 5 on a digit row (y=5..15) would eat them.
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

const HDR = '/home/john/.hermes/workspace/driftseq-fm1base/canvas_extra.h';
const src = fs.readFileSync(HDR, 'utf8');
const insetSrc = /RR_INSET\[36\] = \{([\s\S]*?)\};/.exec(src)[1];
const offSrc = /RR_OFF\[9\] = \{([^}]*)\};/.exec(src)[1];
const INSET = insetSrc.replace(/\/\*[\s\S]*?\*\//g, '').split(',').map((t) => t.trim()).filter(Boolean).map(Number);
const OFF = offSrc.split(',').map((t) => t.trim()).filter(Boolean).map(Number);
if (INSET.length !== 36) throw new Error(`expected 36 insets, parsed ${INSET.length}`);

const PW = 24, PH = 18, PX = 6, PY = 3;
const BG = [24, 24, 32], HUE = [255, 137, 131], INK = BG;

/* the pill, rows only -- same loop as cv_round_rect */
function paint(r, put) {
    for (let i = 0; i < r; i++) {
        const inset = INSET[OFF[r] + i];
        for (let x = inset; x < PW - inset; x++) { put(PX + x, PY + i); put(PX + x, PY + PH - 1 - i); }
    }
    for (let y = r; y < PH - r; y++) for (let x = 0; x < PW; x++) put(PX + x, PY + y);
    /* digit placeholder: two 7px cells centred, knocked out 2px tall marks */
    for (const cx of [5, 12]) for (let y = 5; y <= 15; y++) for (let x = cx; x < cx + 5; x++) if (y === 5 || y === 15) put(PX + x, y, true);
}

console.log('radius  insets              clips digits?  rows of arc');
for (let r = 4; r <= 8; r++) {
    const ins = Array.from({ length: r }, (_, i) => INSET[OFF[r] + i]);
    const clash = ins.some((v, i) => (PY + i >= 5 && PY + i <= 15) && v > 5);
    console.log(`  ${r}     ${ins.join(',').padEnd(20)}${clash ? 'YES' : 'no '}            ${r} top + ${r} bottom`);
}

/* render r=4..8 stacked */
const SCALE = 6, CW = 36, CH = 24, GAP = 6, RADII = [4, 5, 6, 7, 8];
const OW = CW * SCALE, OH = RADII.length * (CH * SCALE + GAP);
const px = new Uint8Array(OW * OH * 3).fill(12);
const putPx = (x, y, c) => {
    if (x < 0 || y < 0 || x >= OW || y >= OH) return;
    const o = (y * OW + x) * 3; px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2];
};
RADII.forEach((r, si) => {
    const oy = si * (CH * SCALE + GAP);
    const put = (x, y, isInk) => {
        for (let dy = 0; dy < SCALE; dy++) for (let dx = 0; dx < SCALE; dx++)
            putPx(x * SCALE + dx, oy + y * SCALE + dy, isInk ? INK : HUE);
    };
    paint(r, put);
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
ihdr.writeUInt32BE(OW, 0); ihdr.writeUInt32BE(OH, 4); ihdr[8] = 8; ihdr[9] = 2;
const raw = Buffer.alloc(OH * (OW * 3 + 1));
for (let y = 0; y < OH; y++) {
    raw[y * (OW * 3 + 1)] = 0;
    Buffer.from(px.buffer, y * OW * 3, OW * 3).copy(raw, y * (OW * 3 + 1) + 1);
}
const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
]);
fs.writeFileSync('/home/john/.hermes/workspace/driftseq-fm1base/docs/pill-radius.png', png);
console.log(`\nwrote docs/pill-radius.png ${OW}x${OH} — top to bottom: r=${RADII.join(', r=')}`);
