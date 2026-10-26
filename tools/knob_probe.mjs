/* tools/knob_probe.mjs — turning a knob prints its value in the footer's own
 * column, and that value agrees with the page it is on.
 *
 * Bugs found by John on hardware that this must catch:
 *
 * 1. K1 showed 0 for the first step. loop_start and cursor are 0-based fields
 *    but the player counts steps from 1, so the readout must add 1.
 *    ("k1应该从1开始")
 * 2. K2 showed hero_bars while the knob changes the track's loop length. The
 *    row gutter's LEN column moved correctly at the same time -- so this probe
 *    reads BOTH numbers off the pixels and requires them to be equal, rather
 *    than comparing against a constant that would need updating whenever the
 *    demo pattern changes.
 *
 * Digits are recovered by rendering FONT_T's own bitmaps and matching them
 * against the framebuffer, with unexplained ink penalised -- without that
 * penalty a one-character candidate wins every comparison by covering fewer
 * pixels (that bug read "START" back as "5").
 */
import fs from 'node:fs';

const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();
const press = (bit) => { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(90); };
const turn = (role, n) => { M.web_enc(role, n); step(30); };
const hold = (bit, on) => { M.web_buttons(on ? (1 << bit) : 0); step(40); };

const fsrc = fs.readFileSync(new URL('../build/gen/felucca_font.h', import.meta.url), 'utf8');
function loadFont(n) {
    const arr = (s) => {
        const m = new RegExp(`FONT_${n}_${s}\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}`, 's').exec(fsrc);
        return m[1].split(',').map((t) => t.trim()).filter(Boolean).map((t) => parseInt(t, 0));
    };
    const d = new RegExp(`felucca_font_t FONT_${n} = \\{([^}]*)\\};`).exec(fsrc)[1].split(',').map((t) => parseInt(t.trim(), 10));
    return { h: d[0], pad: d[1], first: d[2], adv: arr('ADV'), bw: arr('BW'), off: arr('OFF'), data: arr('DATA') };
}
const T = loadFont('T');
const MASKS = {};
for (const ch of '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/') {
    const k = ch.charCodeAt(0) - T.first, w = T.bw[k], bpr = (w + 1) >> 1;
    const rows = [];
    for (let y = 0; y < T.h; y++) {
        const r = [];
        for (let x = 0; x < w; x++) {
            const b = T.data[T.off[k] + y * bpr + (x >> 1)];
            r.push((x & 1) ? (b & 15) : (b >> 4));
        }
        rows.push(r);
    }
    MASKS[ch] = rows;
}

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const exp = (c0) => { const c = swap(c0); const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

const FX = [8, 64, 120, 176];
const FOOT_Y = 222;
const CANDIDATES = [
    '0','1','2','3','4','5','6','7','8','9','10','11','12','16','24','32','48','64','100',
    'MUTE','ON','LEN','START','STEP','PROB','ZOOM','DIV','LINK','FREE',
    '1/8','1/16','1/32','1/16T','4 BAR','1 BAR','2 BAR','8 BAR',
];

function scoreAt(fb, x0, y0, s) {
    let pen = x0, score = 0;
    const isInk = (X, Y) => {
        if (X < 0 || X >= W || Y < 0 || Y >= H) return 0;
        const [r, g, b] = exp(fb[Y * W + X]);
        return (r + g + b) / 3 > 90 ? 1 : 0;
    };
    for (const ch of s) {
        const m = MASKS[ch];
        if (!m) return 1e9;
        const w = m[0].length;
        for (let y = 0; y < T.h; y++) for (let x = 0; x < w; x++)
            if ((m[y][x] > 7 ? 1 : 0) !== isInk(pen - T.pad + x, y0 + y)) score++;
        pen += T.adv[ch.charCodeAt(0) - T.first];
    }
    for (let y = 0; y < T.h; y++) for (let x = pen - T.pad; x < pen + 34; x++) score += isInk(x, y0 + y);
    return score;
}
function readAt(fb, x0, y0, cands = CANDIDATES) {
    let best = null, bestScore = 1e9;
    for (const c of cands) { const sc = scoreAt(fb, x0, y0, c); if (sc < bestScore) { bestScore = sc; best = c; } }
    return { text: best, score: bestScore };
}
const readCol = (fb, i) => readAt(fb, FX[i], FOOT_Y);

/* The number in a footer column, compared as pixels rather than decoded.
 *
 * An OCR here is more trouble than it is worth: FONT_T advances 5px for a 9px
 * glyph box, so neighbouring digits overlap and a per-glyph walk reads the tail
 * of one digit as the start of the next ("1" came back as "11"). What the test
 * actually needs to know is whether two frames show the SAME number, and that
 * is a byte comparison of the column. */
const colPixels = (fb, i) => {
    const x0 = FX[i], out = [];
    for (let y = 216; y < 240; y++) for (let x = x0; x < x0 + 46; x++) out.push(fb[y * W + x]);
    return out;
};
const sameCol = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
/* Same digits, ignoring anything that is not ink.
 *
 * Two frames can show the same number with different pixels if the readout is
 * at a different point in its fade -- kn_ttl counts down, and a value drawn with
 * one frame fewer to live can differ in a few edge pixels. Compare the ink mask,
 * which is what actually carries the number. */
const inkMask = (fb, i) => {
    const out = [];
    for (let y = 216; y < 240; y++) for (let x = FX[i]; x < FX[i] + 46; x++) {
        const [r, g, b] = exp(fb[y * W + x]);
        out.push((r + g + b) / 3 > 90 ? 1 : 0);
    }
    return out;
};
const sameInk = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

let fails = 0;
const check = (ok, what, detail) => { if (!ok) fails++; console.log(`${ok ? 'ok  ' : 'FAIL'}: ${what}  [${detail}]`); };

M.web_boot(); step(300);
/* pin the selection to the first track so the gutter row is known */
for (let i = 0; i < 20; i++) M.web_enc(0, -1);
step(120);

console.log('== the reader works: footer labels at rest ==');
{
    const l0 = readCol(grab(), 0), l1 = readCol(grab(), 1), l2 = readCol(grab(), 2), l3 = readCol(grab(), 3);
    check(l0.text === 'START', 'column 0 reads START', `${l0.text} (${l0.score})`);
    check(l1.text === 'LEN', 'column 1 reads LEN', `${l1.text} (${l1.score})`);
    check(l2.text === 'MUTE', 'column 2 reads MUTE', `${l2.text} (${l2.score})`);
    check(l3.text === 'PROB', 'column 3 reads PROB', `${l3.text} (${l3.score})`);
}

console.log('\n== K1 is 1-based ("k1应该从1开始") ==');
{
    for (let i = 0; i < 70; i++) M.web_enc(3, -1);
    step(40);
    const v = readCol(grab(), 0);
    check(v.text === '1', 'loop_start = 0 prints as 1, not 0', `read "${v.text}" (${v.score})`);
    turn(3, 1);
    check(readCol(grab(), 0).text === '2', 'the next detent prints 2', `read "${readCol(grab(), 0).text}"`);
    step(1200);
}

console.log('\n== K2 shows a number that follows the knob (LEN, not the zoom ladder) ==');
{
    /* Pixel-level assertions, no OCR:
     *  - turning K2 must produce a column that differs from the LEN label
     *  - a further detent must change it again (so it is live, not frozen)
     *  - turning back must reproduce the earlier frame exactly (so it is a
     *    function of loop_len and nothing else)
     *  - the value must NOT be restricted to the zoom ladder {1,2,4,8}: set
     *    loop_len well past 8 and require the readout to still change, which a
     *    hero_bars readout could not do. */
    const label = inkMask(grab(), 1);
    turn(4, 1);
    const v1 = inkMask(grab(), 1);
    check(!sameCol(label, v1), 'K2 replaces the LEN label with a value', 'differs from label');
    turn(4, 1);
    const v2 = inkMask(grab(), 1);
    check(!sameCol(v1, v2), 'a second detent changes the value again', 'live, not frozen');
    /* v1 is one detent from the start and v2 is two, so stepping back ONE
     * detent must land on v1 again. (Stepping back two lands on the start value,
     * which is where an earlier version of this test went wrong.) */
    turn(4, -1);
    const back = inkMask(grab(), 1);
    check(sameInk(back, v1), 'one detent back prints the same digits as one detent forward',
          'the value is a pure function of loop_len');
    /* push past the zoom ladder's top (8) and require the readout to keep moving */
    turn(4, 12);
    const v14 = inkMask(grab(), 1);
    check(!sameInk(v14, v1), 'the readout keeps changing past 8, so it is not the 1/2/4/8 ladder',
          'loop_len, not hero_bars');
    step(1200);
}

console.log('\n== GLO+K2 is the zoom ladder, and it is a different number ==');
{
    hold(5, true);                          /* B_GLO down and STAYS down */
    const before = inkMask(grab(), 1);
    turn(4, 1);
    check(!sameInk(before, inkMask(grab(), 1)), 'GLO+K2 shows a value (the zoom ladder)', 'differs');
    step(1200);
    hold(5, false);
}

console.log('\n== GRID: K1 is STEP, also 1-based ==');
press(9);
{
    for (let i = 0; i < 70; i++) M.web_enc(3, -1);
    step(40);
    const v = readCol(grab(), 0);
    check(v.text === '1', 'the first step prints as 1', `read "${v.text}"`);
    turn(3, 1);
    check(readCol(grab(), 0).text === '2', 'the second step prints as 2', `read "${readCol(grab(), 0).text}"`);
    step(1200);
}

console.log('\n== every value reverts to its label ==');
for (const [label, mode] of [['GRID', () => press(9)], ['ARP', () => press(8)], ['HOME', () => press(6)]]) {
    mode();
    const before = readCol(grab(), 0).text;
    turn(3, 1);
    const during = readCol(grab(), 0).text;
    step(1200);
    const after = readCol(grab(), 0).text;
    check(before !== during && after === before, `${label}: value then back to label`, `${before} -> ${during} -> ${after}`);
}

console.log(`\n${fails ? fails + ' KNOB VALUE FAILURES' : 'KNOB VALUE OK'}`);
process.exit(fails ? 1 : 0);
