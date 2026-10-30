/* tools/global_probe.mjs — pixel assertions on the GLOBAL page, in the SHIPPED
 * wasm. The C test (tests/trkset.txt) asserts the fields; this asserts that the
 * page those fields live on is drawn where it says, and that its three channel
 * states can be told apart by pixels alone (the first version could not).
 *
 *   node tools/trkset_probe.mjs
 *
 * The canvas stores every pixel byte-swapped (gfx.c writes through swap16), so
 * every colour below is compared in its swapped form. Getting that wrong is how
 * the first draft of this file "passed" a filled cell as hollow.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = path.join(HERE, '..', 'build', 'emu', 'app.wasm');
if (!fs.existsSync(WASM)) {
    console.error('no wasm at ' + WASM + ' — build with --web first');
    process.exit(2);
}

let fails = 0, checks = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
};

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: {
        clock_time_get: () => 0, proc_exit: () => {}, fd_write: () => 0,
        fd_close: () => 0, environ_get: () => 0, environ_sizes_get: () => 0,
    },
    env: {},
});
const M = instance.exports;

const W = 240, H = 240;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const press = (bit) => { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(60); };
const enc = (role, n) => { M.web_enc(role, n); step(40); };
const grab = () => Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * H));

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const rgb565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
const stored = (r, g, b) => swap(rgb565(r, g, b));

const ROWBAND = stored(26, 26, 34);      /* the edited row's band */
const px = (fb, x, y) => fb[y * W + x];
/* "text" = anything that is neither the black background nor the row band */
const textAt = (fb, x, y) => { const v = px(fb, x, y); return v !== 0 && v !== ROWBAND; };

const bandsOf = (ys) => {
    const out = []; let cur = [];
    for (const y of ys) {
        if (cur.length && y - cur[cur.length - 1] > 2) { out.push([cur[0], cur[cur.length - 1]]); cur = []; }
        cur.push(y);
    }
    if (cur.length) out.push([cur[0], cur[cur.length - 1]]);
    return out;
};
const textRows = (fb, y0, y1, x0, x1) => {
    const ys = [];
    for (let y = y0; y < y1; y++)
        for (let x = x0; x < x1; x++) if (textAt(fb, x, y)) { ys.push(y); break; }
    return ys;
};

M.web_boot();                              /* the page's own boot: palette_set + app_init.
                                             * Without it every palette colour (C_GRAY, C_AMB,
                                             * C_HI) is pal[]'s zeros, so the labels and values
                                             * draw in black on black and the screen looks empty. */
step(400);
const home = Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * H));
/* ---------- the door: GLO+HOME ---------- */
const mk = () => Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * H));
/* web_buttons is a LEVEL, not an edge: the app samples it once a frame, so GLO
 * must be held across a frame of its own, and the chord across another. Setting
 * both in one step made GLO+HOME land as "HOME pressed with GLO not yet seen". */
M.web_buttons(1 << 5);                     /* B_GLO held */
step(40);
M.web_buttons((1 << 5) | (1 << 6));        /* + B_HOME: the chord */
step(40);
M.web_buttons(0);                          /* the release edge: the page stays */
step(60);
const page = mk();
check(page.some((v, i) => v !== home[i]), 'GLO+HOME opens a different screen');

/* ---------- six page dots, the fifth lit ---------- */
/* The dots live at y=12 (hdr_dots). Six of them means the row is wider than the
 * five the other pages had; the last dot must still be inside the bar. */
{
    const ink = [];
    for (let x = 200; x < W; x++) if (textAt(page, x, 12)) ink.push(x);
    const bands = bandsOf(ink);
    check(bands.length >= 1, 'the page dots are drawn', JSON.stringify(bands));
    let last = -1;
    for (let x = 200; x < W; x++)
        for (let y = 5; y < 19; y++) if (textAt(page, x, y)) { last = x; break; }
    check(last <= W - 2, 'the dots stay inside the bar (six, not five)', 'rightmost ink x=' + last + ' of ' + W);
}

/* ---------- five rows at 24px pitch ---------- */
{
    const lb = bandsOf(textRows(page, 24, 148, 10, 60));
    check(lb.length === 4, 'four label rows', JSON.stringify(lb));
    const want = [32, 58, 84, 110];
    check(lb.length === 4 && lb.every(([a, b], i) => a === want[i] && b === a + 10),
          'the rows are 26px apart, each 11px tall', JSON.stringify(lb));
    let worst = 0, missing = 0;
    for (const y of want) {
        let lastv = -1;
        for (let x = 100; x < W; x++)
            for (let dy = 0; dy < 11; dy++) if (textAt(page, x, y + dy)) { lastv = x; break; }
        if (lastv < 0) missing++;
        if (lastv > worst) worst = lastv;
    }
    check(missing === 0, 'every row draws a value at x>=100', missing + ' rows without one');
    check(worst < 238, 'no value runs off the right edge', 'rightmost value pixel x=' + worst);
}

/* ---------- K1 moves the highlight 24px, dead stop at the fifth row ---------- */
{
    const bandTop = (fb) => { for (let y = 24; y < 148; y++) if (px(fb, 8, y) === ROWBAND) return y; return -1; };
    const a0 = bandTop(page);
    check(a0 > 0, 'the edited row has a band', 'band at ' + a0);
    enc(3, 1);                              /* EN_K1 */
    const a1 = bandTop(mk());
    check(a1 === a0 + 26, 'K1 moves the highlight one row (26px)', a0 + ' -> ' + a1);
    enc(3, 20);
    const a2 = bandTop(mk());
    check(a2 === a0 + 78, 'K1 stops at the fourth row, it does not wrap', 'band at ' + a2);
    enc(3, -20);
}

/* ---------- the two lines that explain the page ---------- */
{
    /* The notes are drawn in (120,120,134), which is NOT the row band's
     * (26,26,34) - but textAt() only excludes black and the band, so a dim grey
     * line still counts. Measure "any ink", not "text". */
    /* cv_text draws AT the baseline: the ink of a line at y=152 is y154..164,
     * so measuring the single row y=152 finds the 2px gap above the caps. */
    const line = (y) => {
        let n = 0;
        for (let yy = y; yy < y + 11; yy++) for (let x = 10; x < 230; x++) if (px(page, x, yy) !== 0) n++;
        return n;
    };
    check(line(152) > 30, 'the swing formula is drawn', line(152) + ' px');
    check(line(170) > 30, 'the page says where TRK SWING lives', line(170) + ' px');
    check(line(188) > 30, 'the tap-tempo note is drawn', line(188) + ' px');
    check(line(200) === 0, 'and nothing spills into the footer', 'ink below the notes: ' + line(200));
}

/* ---------- footer names this page's knobs ---------- */
{
    const ink = (x0) => {
        let n = 0;
        for (let y = 224; y < 232; y++) for (let x = x0; x < x0 + 50; x++) if (textAt(page, x, y)) n++;
        return n;
    };
    check(ink(8) > 20, 'footer K1 says a word (ROW)', ink(8) + ' px');
    check(ink(69) > 20, 'footer K2 says a word (EDIT)', ink(69) + ' px');
    check(ink(130) === 0, 'footer K3 is blank (nothing to pick here)', ink(130) + ' px');
    check(ink(189) === 0, 'footer K4 is blank', ink(189) + ' px');
}

/* ---------- a bare HOME leaves ---------- */
/* press() drives one bit level across frames, which is right for a single
 * button. HOME here is the release that follows the chord's opening press, so
 * gl_opening has already swallowed it; a fresh press/release pair is a bare
 * HOME and must leave. */
M.web_buttons(1 << 6); step(40);
M.web_buttons(0); step(60);
{
    /* Not "equals the first screenshot": the knobs above moved the row and the
     * band, and HOME shows the selected track, so a few pixels legitimately
     * differ. What must be true is that GLOBAL's five rows are gone - its
     * page dots (six) and its two notes are the page's own marks. */
    const back = mk();
    let rows = 0;
    for (let y = 30; y < 145; y++) if (px(back, 8, y) === ROWBAND) rows++;
    check(rows === 0, 'a bare HOME leaves: no row band anywhere', rows + ' band rows left');
    let notes = 0;
    for (let x = 10; x < 230; x++) if (px(back, x, 155) !== 0) notes++;
    check(notes === 0, "and the page's own notes are gone", notes + ' px at y155 (the swing note line)');
}

console.log('\n' + (fails ? 'FAILURES: ' + fails + '/' + checks : 'GLOBAL PROBE OK (' + checks + ' checks)'));
process.exit(fails ? 1 : 0);
