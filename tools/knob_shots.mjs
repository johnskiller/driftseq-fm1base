/* tools/knob_shots.mjs — the footer with a knob in hand vs at rest, plus the
 * four pages' headers, all from the built wasm.
 *
 * Left column: HOME and GRID as they render normally.
 * Right column: the same two frames captured two frames after a detent, so the
 * footer column holding the value is visible next to its label version.
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
const press = (bit) => { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(90); };
const turn = (role, n) => { M.web_enc(role, n); step(30); };

M.web_boot(); step(300);

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const exp = (c0) => { const c = swap(c0); const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

const rows = [];
/* HOME at rest, then with K1 held (value showing in the START column) */
rows.push([['HOME   at rest', grab()]]);
turn(3, 3);
rows.push([['HOME   K1 turned  -> value in place of START', grab()]]);
step(1500);                                  /* let it revert */

press(9);
rows.push([['GRID   at rest', grab()]]);
turn(4, 2);
rows.push([['GRID   K2 turned  -> value in place of LEN', grab()]]);
step(1500);

press(8);
rows.push([['ARP    at rest', grab()]]);
turn(5, 4);
rows.push([['ARP    K3 turned  -> value in place of ROT', grab()]]);
step(1500);

/* The footer is the point of this sheet, so crop each frame to its bottom band
 * (y 196..240) at 3x, where the difference is legible, and stack them. */
const CROPY = 196, CROPH = 44, SC = 3;
const PADX = 8, PADY = 6, LBL = 12;
const bandW = W * SC, bandH = CROPH * SC;
const OW = bandW + PADX * 2;
const OH = rows.length * (bandH + PADY + LBL) + PADY;
const rgb = Buffer.alloc(OW * OH * 3);
for (let i = 0; i < OW * OH; i++) { rgb[i * 3] = 10; rgb[i * 3 + 1] = 10; rgb[i * 3 + 2] = 13; }
const set = (x, y, r, g, b) => { if (x < 0 || x >= OW || y < 0 || y >= OH) return;
    const i = (y * OW + x) * 3; rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b; };

const BLOCK = {
    A: ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    D: ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
    E: ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
    G: ['.###.', '#...#', '#....', '#.###', '#...#', '#...#', '.###.'],
    H: ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    I: ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '#####'],
    L: ['#....', '#....', '#....', '#....', '#....', '#....', '#####'],
    N: ['#...#', '##..#', '#.#.#', '#..##', '#...#', '#...#', '#...#'],
    O: ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    P: ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
    R: ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
    S: ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    T: ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    U: ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    V: ['#...#', '#...#', '#...#', '#...#', '#...#', '.#.#.', '..#..'],
    '-': ['.....', '.....', '.....', '#####', '.....', '.....', '.....'],
    '>': ['#....', '.#...', '..#..', '...#.', '..#..', '.#...', '#....'],
};

rows.forEach(([[label, fb]], idx) => {
    const ty = idx * (bandH + PADY + LBL) + PADY;
    let lx = PADX;
    for (const ch of label) {
        if (ch === ' ') { lx += 4; continue; }
        const g = BLOCK[ch];
        if (g) for (let yy = 0; yy < 7; yy++) for (let xx = 0; xx < 5; xx++)
            if (g[yy][xx] === '#') set(lx + xx, ty, 255, 196, 90);
        lx += 6;
    }
    const by = ty + LBL;
    for (let y = 0; y < CROPH; y++) for (let x = 0; x < W; x++) {
        const [r, g, b] = exp(fb[(CROPY + y) * W + x]);
        for (let dy = 0; dy < SC; dy++) for (let dx = 0; dx < SC; dx++)
            set(PADX + x * SC + dx, by + y * SC + dy, r, g, b);
    }
});

const out = Buffer.alloc(OH * (OW * 3 + 1));
let p = 0;
for (let y = 0; y < OH; y++) { out[p++] = 0;
    for (let x = 0; x < OW; x++) { const i = (y * OW + x) * 3; out[p++] = rgb[i]; out[p++] = rgb[i + 1]; out[p++] = rgb[i + 2]; } }
function crc32(buf) { let c, crc = 0xffffffff;
    for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 0xff;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crc = (crc >>> 8) ^ c; }
    return (crc ^ 0xffffffff) >>> 0; }
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length);
    const td = Buffer.concat([Buffer.from(t, 'ascii'), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td));
    return Buffer.concat([l, td, c]); };
const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(OW, 0); ihdr.writeUInt32BE(OH, 4); ihdr[8] = 8; ihdr[9] = 2;
const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(out)), chunk('IEND', Buffer.alloc(0))]);
const dest = process.argv[2] || '/tmp/knob_shots.png';
fs.writeFileSync(dest, png);
console.log(`${OW}x${OH} -> ${dest} (${png.length} B)`);
