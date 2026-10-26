/* tools/header_stable.mjs — the header must be pixel-identical on every track.
 *
 * John: "track 1-9和10-16药丸大小要保持一致，避免header跳动".
 *
 * A pill fitted to the number grows by one digit's advance at track 10 and
 * pushes the context text, the dots and the momentum all right. That pixel
 * shift is what he is objecting to, so this measures the shift directly:
 * for each of the 16 tracks, record the leftmost ink column of the context
 * text (everything right of the pill), and require all 16 to be equal.
 */
import fs from 'node:fs';
const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();
M.web_boot(); step(300);

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const BG = swap(((24 >> 3) << 11) | ((24 >> 2) << 5) | (32 >> 3));   /* TB_BG in fb order */

/* the pill sits at x=6..29; everything from x=32 right is the context text */
function contextLeft(fb) {
    for (let x = 32; x < W; x++)
        for (let y = 3; y < 23; y++)
            if (fb[y * W + x] !== BG) return x;
    return -1;
}
function dotsLeft(fb) {
    /* the page dots are the rightmost cluster, at x>=211; find the first lit dot */
    for (let x = 211; x < W; x++)
        for (let y = 8; y < 20; y++) {
            const c = fb[y * W + x];
            if (c !== BG) return x;
        }
    return -1;
}
/* the pill's own right edge: last non-background column before x=32 */
function pillRight(fb) {
    for (let x = 31; x >= 0; x--)
        for (let y = 3; y < 23; y++)
            if (fb[y * W + x] !== BG) return x;
    return -1;
}

let fails = 0;
const check = (ok, what, detail) => { if (!ok) fails++; console.log(`${ok ? 'ok  ' : 'FAIL'}: ${what}  [${detail}]`); };

const ctx = [], pr = [];
for (let t = 0; t < 16; t++) {
    if (t > 0) { M.web_enc(0, 1); step(60); }      /* EN_SELECT: next track */
    const fb = grab();
    ctx.push(contextLeft(fb));
    pr.push(pillRight(fb));
}
const uniqCtx = [...new Set(ctx)];
const uniqPill = [...new Set(pr)];
check(uniqCtx.length === 1, 'the context text starts at the same x on all 16 tracks',
      `x = ${uniqCtx.join(',')} over tracks 1..16`);
check(uniqPill.length === 1, 'the pill has the same right edge on all 16 tracks',
      `x = ${uniqPill.join(',')}, so its width does not depend on the digit count`);

/* and the specific case John named: 9 vs 10 */
{
    const at9 = ctx[8], at10 = ctx[9];
    check(at9 === at10, 'track 9 and track 10 put the context text at the same x',
          `9 -> x=${at9}, 10 -> x=${at10}`);
}

console.log(`\n${fails ? fails + ' HEADER STABILITY FAILURES' : 'HEADER STABLE'}`);
process.exit(fails ? 1 : 0);
