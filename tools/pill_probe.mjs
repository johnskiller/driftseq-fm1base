/* tools/pill_probe.mjs — the pill's corner is a solid arc, not a chamfer with a
 * hole in it.
 *
 * John saw the first version on hardware: "药丸圆角矩形不对，现在是切角内加了黑点".
 * The old cv_round_rect plotted one pixel per row along the diagonal and left
 * the body inset by `cut`, so each corner was a detached dot with background
 * showing between it and the fill.
 *
 * Three things must hold, and each one failed on that version:
 *   1. no background pixel inside the pill's bounding box (the "黑点")
 *   2. the corner is monotone: insets shrink toward the middle, never grow
 *   3. the top row is narrower than the middle row (it is a radius, not a cut)
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
const rgb = (c0) => { const c = swap(c0); const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

let fails = 0;
const check = (ok, label, detail = '') => {
    if (!ok) fails++;
    console.log(`${ok ? 'ok  :' : 'FAIL:'} ${label}${detail ? `  [${detail}]` : ''}`);
};

const PX = 6, PY = 3, PW = 24, PH = 18;
function rows(fb) {
    const bg = rgb(fb[(PY - 2) * W + 120]);
    const isBg = (c) => Math.abs(c[0] - bg[0]) + Math.abs(c[1] - bg[1]) + Math.abs(c[2] - bg[2]) < 14;
    const out = [];
    for (let y = 0; y < PH; y++) {
        let l = -1, r = -1, holes = 0;
        for (let x = 0; x < PW; x++) {
            if (!isBg(rgb(fb[(PY + y) * W + PX + x]))) { if (l < 0) l = x; r = x; }
        }
        /* holes = background pixels strictly between the left and right edges */
        if (l >= 0) for (let x = l; x <= r; x++)
            if (isBg(rgb(fb[(PY + y) * W + PX + x]))) holes++;
        out.push({ l, r, holes });
    }
    return out;
}

console.log('== the pill is a solid rounded rect ==');
{
    const R = rows(grab());
    check(R.every((v) => v.l >= 0), 'the pill is present on every row of its box');

    /* 1. no holes. The digits are knocked out in the background colour, so
     *    count holes only outside the digit column (x 4..20 of the pill). */
    const digitHoles = R.map((v, y) => (y >= 4 && y <= 15) ? 0 : v.holes);
    check(digitHoles.every((h) => h === 0), 'no background gaps inside the pill body',
          `hole pixels per row: ${digitHoles.join(',')}`);

    /* 2. the corner is an arc: inset shrinks monotonically toward the middle */
    const inset = R.map((v) => v.l);
    const top = inset.slice(0, 4), bot = inset.slice(-4).reverse();
    const mono = (a) => a.every((v, i) => i === 0 || v <= a[i - 1]);
    check(mono(top), 'the top-left corner narrows monotonically', `${top.join(',')}`);
    check(mono(bot), 'the bottom-left corner narrows monotonically', `${bot.join(',')}`);
    check(top[0] > top[top.length - 1], 'the top row is narrower than the rows below it',
          `${top[0]} -> ${top[top.length - 1]}`);

    /* 3. mirror symmetry: the arc must match left to right and top to bottom */
    const mirrorX = R.every((v) => v.l === (PW - 1 - v.r));
    check(mirrorX, 'the left and right arcs mirror each other');
    const mirrorY = R.every((v, y) => v.l === R[PH - 1 - y].l);
    check(mirrorY, 'the top and bottom arcs mirror each other');

    /* 4. the widest row is the full pill width -- the body is not inset */
    check(R.some((v) => v.r - v.l + 1 === PW), 'some row spans the full 24px width',
          `widest = ${Math.max(...R.map((v) => v.r - v.l + 1))}`);
}

console.log('\n== the arc is identical on one- and two-digit tracks ==');
{
    const a = rows(grab()).map((v) => `${v.l},${v.r}`).join(' ');
    for (let i = 0; i < 32; i++) M.web_enc(0, -1);
    step(20);
    for (let i = 0; i < 9; i++) M.web_enc(0, 1);
    step(60);
    const b = rows(grab()).map((v) => `${v.l},${v.r}`).join(' ');
    check(a === b, 'track 1 and track 10 have the same pill outline', 'edges match row for row');
}

console.log(fails ? `\nPILL FAILED (${fails})` : '\nPILL OK');
process.exit(fails ? 1 : 0);
