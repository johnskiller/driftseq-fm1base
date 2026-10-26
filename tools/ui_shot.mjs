/* tools/ui_shot.mjs — the emulator's screen to a PNG.
 *
 * The wasm build is the authoritative renderer: it runs the same painters the
 * firmware does, so a PNG from here is what the device draws, minus the panel
 * hardware. A pixel assertion proves a property; this proves the picture, which
 * is the only way to catch a layout that is wrong in a way nobody thought to
 * assert.
 *
 *   node tools/ui_shot.mjs                 -> /tmp/ds_screen.png (home)
 *   node tools/ui_shot.mjs out.png
 *   node tools/ui_shot.mjs out.png 3       -> 3x nearest-neighbour, for looking at 1px detail
 *
 * Zero dependencies: the PNG encoder below is zlib plus two CRCs.
 */
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = path.join(HERE, '..', 'build', 'emu', 'app.wasm');

const OUT = process.argv[2] || '/tmp/ds_screen.png';
const SCALE = Math.max(1, parseInt(process.argv[3] || '1', 10) || 1);

if (!fs.existsSync(WASM)) {
    console.error('no wasm at ' + WASM + ' — build with --web first');
    process.exit(2);
}

/* ---- CRC32 (PNG chunk checksums) ---- */
const CRC = (() => {
    const t = new Int32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c;
    }
    return (buf) => {
        let c = -1;
        for (const b of buf) c = t[(c ^ b) & 0xFF] ^ (c >>> 8);
        return (c ^ -1) >>> 0;
    };
})();

function chunk(type, data) {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(CRC(body));
    return Buffer.concat([len, body, crc]);
}

function encodePNG(w, h, rgb) {
    const raw = Buffer.alloc((w * 3 + 1) * h);
    for (let y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;                        /* filter: none */
        rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
    }
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0);
    ihdr.writeUInt32BE(h, 4);
    ihdr[8] = 8;                                          /* bit depth */
    ihdr[9] = 2;                                          /* colour type: truecolour */
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        chunk('IHDR', ihdr),
        chunk('IDAT', zlib.deflateSync(raw)),
        chunk('IEND', Buffer.alloc(0)),
    ]);
}

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: {
        clock_time_get: () => 0, proc_exit: () => {}, fd_write: () => 0,
        fd_close: () => 0, environ_get: () => 0, environ_sizes_get: () => 0,
    },
    env: {},
});
const M = instance.exports;

/* web_render(n) counts SAMPLE FRAMES, and run_ms(1) renders 44.1 of them, so
 * n/44.1 is the number of emulated milliseconds. */
function step(ms) {
    const n = Math.round(ms * 44.1);
    for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i));
}

M.web_boot();
step(200);                        /* past the splash, so a frame has been drawn */

const W = 240, H = 240;
const fb = new Uint16Array(M.memory.buffer, M.web_fb(), W * H);

/* RGB565 -> RGB888, expanded so white stays white (replicated high bits) */
function expand(c) {
    const r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)];
}

const ow = W * SCALE, oh = H * SCALE;
const rgb = Buffer.alloc(ow * oh * 3);
for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
        const [r, g, b] = expand(fb[y * W + x]);
        for (let sy = 0; sy < SCALE; sy++) {
            for (let sx = 0; sx < SCALE; sx++) {
                const o = ((y * SCALE + sy) * ow + (x * SCALE + sx)) * 3;
                rgb[o] = r; rgb[o + 1] = g; rgb[o + 2] = b;
            }
        }
    }
}

fs.writeFileSync(OUT, encodePNG(ow, oh, rgb));
console.log(`${ow}x${oh} ${fs.statSync(OUT).size} B -> ${OUT}`);
