/* tools/header_shots.mjs — the real screens after the header change: HOME,
 * GRID melodic, GRID drum, ARP. Captured from the built wasm, 2x, labelled.
 * Evidence, not a mock: every pixel here comes out of the firmware's painter.
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

M.web_boot(); step(300);

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const exp = (c0) => { const c = swap(c0); const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

/* 5x7 block font, only the letters the labels need */
const BLOCK = {
    A: ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    B: ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
    C: ['.###.', '#...#', '#....', '#....', '#....', '#...#', '.###.'],
    D: ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
    E: ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
    G: ['.###.', '#...#', '#....', '#.###', '#...#', '#...#', '.###.'],
    H: ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    I: ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '#####'],
    L: ['#....', '#....', '#....', '#....', '#....', '#....', '#####'],
    M: ['#...#', '##.##', '#.#.#', '#...#', '#...#', '#...#', '#...#'],
    O: ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    P: ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
    R: ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
    U: ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
};

const shots = [];
shots.push(['HOME', grab()]);
press(9); shots.push(['GRID  melodic', grab()]);
/* the drum grid is reached by selecting a drum track on HOME; press HOME, move
 * the track selection to a drum track, then SEQ. Track 02 is the drum track in
 * the demo pattern (channel 9), so K3 down once selects it. */
press(6);
M.web_enc(6, 1); step(120);          /* EN_K4 -> next track */
press(9); shots.push(['GRID  drum', grab()]);
press(8); shots.push(['ARP', grab()]);
press(6);

/* ---- compose: 2 columns x 2 rows, each screen 2x, with a label strip ---- */
const SC = 2, PAD = 6, LBL = 11;
const CW = W * SC + PAD * 2, CH = H * SC + PAD * 2 + LBL;
const OW = CW * 2, OH = CH * 2;
const rgb = Buffer.alloc(OW * OH * 3);
const set = (x, y, r, g, b) => { if (x < 0 || x >= OW || y < 0 || y >= OH) return;
    const i = (y * OW + x) * 3; rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b; };

for (let i = 0; i < OW * OH; i++) { rgb[i * 3] = 10; rgb[i * 3 + 1] = 10; rgb[i * 3 + 2] = 13; }

shots.forEach(([label, fb], idx) => {
    const cx = (idx % 2) * CW + PAD, cy = Math.floor(idx / 2) * CH + PAD + LBL;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        const [r, g, b] = exp(fb[y * W + x]);
        for (let dy = 0; dy < SC; dy++) for (let dx = 0; dx < SC; dx++)
            set(cx + x * SC + dx, cy + y * SC + dy, r, g, b);
    }
    /* 1px frame so the 240x240 edge is visible against the dark sheet */
    for (let x = -1; x <= W * SC; x++) { set(cx + x, cy - 1, 70, 70, 82); set(cx + x, cy + H * SC, 70, 70, 82); }
    for (let y = -1; y <= H * SC; y++) { set(cx - 1, cy + y, 70, 70, 82); set(cx + W * SC, cy + y, 70, 70, 82); }
    /* label: 5x7 block letters, drawn by hand so this file needs no font */
    const ly = Math.floor(idx / 2) * CH + 2;
    let lx = cx;
    for (const ch of label) {
        if (ch === ' ') { lx += 4; continue; }
        const rows = BLOCK[ch];
        if (rows) for (let yy = 0; yy < 7; yy++) for (let xx = 0; xx < 5; xx++)
            if (rows[yy][xx] === '#') set(lx + xx, ly + yy, 255, 196, 90);
        lx += 6;
    }
});

/* ---- write the PNG ---- */
const out = Buffer.alloc(OH * (OW * 3 + 1));
let p = 0;
for (let y = 0; y < OH; y++) {
    out[p++] = 0;
    for (let x = 0; x < OW; x++) { const i = (y * OW + x) * 3; out[p++] = rgb[i]; out[p++] = rgb[i + 1]; out[p++] = rgb[i + 2]; }
}
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
const dest = process.argv[2] || '/tmp/header_shots.png';
fs.writeFileSync(dest, png);
console.log(`${OW}x${OH} -> ${dest} (${png.length} B)`);
console.log('captured:', shots.map(([l]) => l).join(' | '));
