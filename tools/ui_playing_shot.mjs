/* tools/ui_playing_shot.mjs — a PNG of the drum grid WHILE it plays.
 *
 * The playhead only exists during playback, so tools/ui_page_shot.mjs (which
 * presses a button and shoots) never captures it. This one starts the
 * sequencer, lets it run, then walks to the grid.
 *
 *   node tools/ui_playing_shot.mjs /tmp/drum.png [scale] [ms]
 */
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = path.join(HERE, '..', 'build', 'emu', 'app.wasm');
const OUT = process.argv[2] || '/tmp/ds_playing.png';
const SCALE = Math.max(1, parseInt(process.argv[3] || '3', 10) || 3);
const MS = parseInt(process.argv[4] || '2300', 10);

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: { clock_time_get: () => 0, proc_exit: () => {}, fd_write: () => 0, fd_close: () => 0, environ_get: () => 0, environ_sizes_get: () => 0 },
    env: {},
});
const M = instance.exports;
const W = 240, H = 240;

function step(ms) {
    const n = Math.round(ms * 44.1);
    for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i));
}
function press(bit) { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(60); }

M.web_boot();
step(200);
press(10);          /* B_PLAY */
step(MS);
press(9);           /* B_SEQ: track 0 is ch9, so this is the drum grid */
step(120);

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

function encodePNG(w, h, rgb) {
    const crcT = new Int32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; crcT[n] = c; }
    const crc = (buf) => { let c = -1; for (const x of buf) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
    const chunk = (t, d) => {
        const l = Buffer.alloc(4); l.writeUInt32BE(d.length);
        const td = Buffer.concat([Buffer.from(t), d]);
        const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
        return Buffer.concat([l, td, c]);
    };
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
    const raw = Buffer.alloc(h * (w * 3 + 1));
    for (let y = 0; y < h; y++) rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
                          chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)),
                          chunk('IEND', Buffer.alloc(0))]);
}

fs.writeFileSync(OUT, encodePNG(ow, oh, rgb));
console.log(`playing drum grid: ${ow}x${oh} ${fs.statSync(OUT).size} B -> ${OUT}`);
