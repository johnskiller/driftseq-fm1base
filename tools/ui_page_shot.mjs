/* tools/ui_page_shot.mjs — a PNG of any UI page, by pressing its button first.
 *
 * tools/ui_shot.mjs shows what boots (HOME). This one walks the screens, which
 * is what you need when the change is a layout: a page that never got drawn and
 * a page that drew the wrong thing look identical at HOME.
 *
 *   node tools/ui_page_shot.mjs grid   /tmp/grid.png  [scale]
 *   node tools/ui_page_shot.mjs edit   /tmp/edit.png
 *   node tools/ui_page_shot.mjs arp    /tmp/arp.png
 *   node tools/ui_page_shot.mjs home   /tmp/home.png
 */
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = path.join(HERE, '..', 'build', 'emu', 'app.wasm');

const PAGE = (process.argv[2] || 'home').toLowerCase();
const OUT = process.argv[3] || '/tmp/ds_' + PAGE + '.png';
const SCALE = Math.max(1, parseInt(process.argv[4] || '2', 10) || 2);

/* plat.h: B_HOME=6, B_EDIT=4, B_ARP=8, B_SEQ=9 */
const ROUTE = {
    home: [],
    grid: [9],
    edit: [9, 4],
    arp: [8],
};

if (!fs.existsSync(WASM)) {
    console.error('no wasm at ' + WASM + ' — build with --web first');
    process.exit(2);
}
if (!(PAGE in ROUTE)) {
    console.error('page must be one of: ' + Object.keys(ROUTE).join(', '));
    process.exit(2);
}

const CRC = (() => {
    const t = new Int32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c; }
    return (buf) => { let c = -1; for (const b of buf) c = t[(c ^ b) & 0xFF] ^ (c >>> 8); return (c ^ -1) >>> 0; };
})();
function chunk(type, data) {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(CRC(body));
    return Buffer.concat([len, body, crc]);
}
function encodePNG(w, h, rgb) {
    const raw = Buffer.alloc((w * 3 + 1) * h);
    for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3); }
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
    return Buffer.concat([Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: { clock_time_get: () => 0, proc_exit: () => {}, fd_write: () => 0, fd_close: () => 0, environ_get: () => 0, environ_sizes_get: () => 0 },
    env: {},
});
const M = instance.exports;

function step(ms) {
    const n = Math.round(ms * 44.1);
    for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i));
}
function press(bit) { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(60); }

M.web_boot();
step(200);
for (const b of ROUTE[PAGE]) press(b);

const W = 240, H = 240;
const fb = new Uint16Array(M.memory.buffer, M.web_fb(), W * H);
const ow = W * SCALE, oh = H * SCALE;
const rgb = Buffer.alloc(ow * oh * 3);
for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
        const c = fb[y * W + x];
        const r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        const R = (r << 3) | (r >> 2), G = (g << 2) | (g >> 4), B = (b << 3) | (b >> 2);
        for (let sy = 0; sy < SCALE; sy++)
            for (let sx = 0; sx < SCALE; sx++) {
                const o = ((y * SCALE + sy) * ow + (x * SCALE + sx)) * 3;
                rgb[o] = R; rgb[o + 1] = G; rgb[o + 2] = B;
            }
    }
}
fs.writeFileSync(OUT, encodePNG(ow, oh, rgb));
console.log(`${PAGE}: ${ow}x${oh} ${fs.statSync(OUT).size} B -> ${OUT}`);
