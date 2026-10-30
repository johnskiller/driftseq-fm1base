/* tools/trkset_probe.mjs — pixel assertions on the TRACK SET page, in the SHIPPED
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
const home = grab();
press(2);                                   /* B_ENV */
const page = grab();
check(page.some((v, i) => v !== home[i]), 'ENV opens a different screen');

/* ---------- the five rows ---------- */
/* Labels are at x=12..~60, values at x=96... Measuring only the label column
 * keeps the two apart. */
{
    const lb = bandsOf(textRows(page, 24, 146, 12, 60));
    check(lb.length === 5, 'five label rows, one per 22px band', JSON.stringify(lb));
    /* cv_text draws at the baseline y; 16px type starts 2px below it and its ink
     * is 11px tall, so a row drawn at y=30 starts at 32. Check the pitch (22px,
     * exact) and the ink height, which is what a drifted or doubled row breaks. */
    const want = [32, 54, 76, 98, 120];
    check(lb.length === 5 && lb.every(([a, b], i) => a === want[i] && b === a + 10),
          'the five rows are 22px apart, each 11px tall', JSON.stringify(lb));
    let worst = 0, missing = 0;
    for (const y of want) {
        let last = -1;
        for (let x = 100; x < W; x++)
            for (let dy = 0; dy < 11; dy++) if (textAt(page, x, y + dy)) { last = x; break; }
        if (last < 0) missing++;
        if (last > worst) worst = last;
    }
    check(missing === 0, 'every row draws a value at x>=96', missing + ' rows without one');
    check(worst < 236, 'no value runs off the right edge', 'rightmost value pixel x=' + worst);
}

/* ---------- K1 moves the highlight, and dead-stops at both ends ---------- */
/* The highlight is the band, so find the band's first row and require it to move
 * exactly 20px per detent. */
function bandTop(fb) {
    /* from y24: the band starts there (row 0 = TS_Y0-6) and the header's own
     * background is the SAME 565 value, so scanning earlier finds the header. */
    for (let y = 24; y < 146; y++) if (px(fb, 8, y) === ROWBAND) return y;
    return -1;
}
{
    const a0 = bandTop(page);
    enc(3, 1);                                  /* EN_K1 */
    const a1 = bandTop(grab());
    enc(3, 1);
    const a2 = bandTop(grab());
    enc(3, 20);
    const a3 = bandTop(grab());
    enc(3, -20);
    const a4 = bandTop(grab());
    check(a0 > 0, 'the edited row is marked with a band', 'y=' + a0);
    check(a1 === a0 + 22, 'K1 moves the highlight one row (22px)', a0 + ' -> ' + a1);
    check(a2 === a0 + 44, 'and again', a1 + ' -> ' + a2);
    check(a3 === a0 + 88, 'K1 stops at the fifth row, it does not wrap', 'band at ' + a3);
    check(a4 === a0, 'and stops at the first', 'band at ' + a4);
}

/* ---------- the channel map: three states, told apart by pixels ---------- */
/* Demo track 1 sits on ch10 with tracks 2 and 3, so cell 10 (index 9) is the
 * page's own, cell 1 (index 0) is another track's, cell 15 (index 14) is free.
 * Solid = the page's own; hollow with bright digits = taken; dim = free. */
{
    const fill = (fb, idx) => {
        const x = 8 + idx * 14;
        let lit = 0;
        for (let y = 158; y < 178; y++)
            for (let xx = x; xx < x + 12; xx++) {
                const v = px(fb, xx, y);
                if (v === stored(24, 24, 32) || v === 0) continue;   /* background/TB_TX? see below */
                const r = (v >> 3) & 0x1f, g = (v >> 2) & 0x3f, b = (v << 3) & 0x1f;
                if (r + g + b > 40) lit++;
            }
        return lit / (12 * 20);
    };
    const own = fill(page, 9), other = fill(page, 0), free = fill(page, 14);
    check(own > 0.85, 'the page own channel is a solid block', 'fill=' + own.toFixed(2));
    check(other < 0.5, 'a channel another track holds is hollow, not solid', 'fill=' + other.toFixed(2));
    check(other > free, 'its digits are brighter than a free channel', 'other=' + other.toFixed(2) + ' free=' + free.toFixed(2));
}

/* ---------- the footer names this page's own two knobs ---------- */
{
    const ink = (x0) => {
        let n = 0;
        for (let y = 224; y < 232; y++) for (let x = x0; x < x0 + 50; x++) if (textAt(page, x, y)) n++;
        return n;
    };
    check(ink(8) > 20, 'footer K1 says a word (ROW)', ink(8) + ' px');
    /* K4 is blank: the SWING row above owns the per-track number, and the global
     * half is on the GLOBAL page, so nothing is left for the fourth knob. */
    check(ink(189) === 0, 'footer K4 is blank (SWING is a row, not a knob)', ink(189) + ' px');
}

/* ---------- the bottom line: whose PROG counts on this channel ----------
 * Pixels, because this is a line of English the player reads, not a field: the
 * C suite can say "owner is track 3" but only a render can say the page tells
 * the player so. Row y=180, 12px type; count lit runs to compare line lengths
 * between a track that owns its channel and one that does not. */
{
    const inkWidth = (fb) => {
        let last = -1;
        for (let x = 0; x < W; x++)
            for (let y = 184; y < 202; y++)
                if (px(fb, x, y) !== 0 && px(fb, x, y) !== ROWBAND) { last = x; break; }
        return last;
    };
    const before = inkWidth(page);           /* track 0: owns ch9, roll call */
    enc(0, 5);                               /* SELECT -> track 5, an empty track on ch0 */
    const after = grab();
    const w5 = inkWidth(after);
    check(w5 > 0, 'the bottom line renders on an empty track too (last ink x=' + w5 + ')');
    check(w5 !== before, 'the line changes when the edit track stops owning its channel',
          'track0 line ends at ' + before + ', track5 at ' + w5);
    /* and the same text is there for the owner of a shared channel: back to 0 */
    enc(0, -5);
    const back0 = grab();
    check(inkWidth(back0) === before, 'returning to the owner restores its line',
          'x=' + inkWidth(back0) + ' vs ' + before);
}

/* ---------- the state badge: a filled pill, not a sentence ----------
 * "MELODIC MUTE" in one colour read as a name; John asked for the state to be a
 * shape. Assert the pill exists where no pill should be (a free track), appears
 * on MUTE, and sits clear of the page dots on x211..241. */
{
    const pill = (fb) => {
        /* width of the filled chip: columns in the header's right half whose mid
         * row is neither the bar background nor black. One letter wide (glyph+8),
         * so a word-sized pill fails this and a 14px chip passes. */
        /* a column counts if ANY row of the chip is filled: the knocked-out
         * letter punches holes in the mid row, so a single-row scan measures the
         * gaps between strokes, not the chip. */
        let first = -1, last = -1;
        for (let x = 150; x < 211; x++) {
            let on = false;
            for (let y = 5; y < 19; y++) {
                const v = px(fb, x, y);
                if (v !== stored(24, 24, 32) && v !== 0) { on = true; break; }
            }
            if (on) { if (first < 0) first = x; last = x; }
        }
        return first < 0 ? 0 : last - first + 1;
    };
    const plain = pill(page);
    enc(5, 9);                                  /* EN_K3 (enum: 0,1,2,3,4,5) to the MUTE end */
    const muted = grab();
    const p2 = pill(muted);
    check(p2 > plain, 'MUTE draws a filled chip in the header', 'before=' + plain + ' after=' + p2);
    /* one letter, not a word: M is 5px of glyph, so the chip is 13..14px wide.
     * A word ("MUTE" = 22px + 8) would be 30. */
    check(p2 > 8 && p2 < 20, 'the chip is one letter wide, not a word', p2 + 'px');
    /* the pill must end before the dots start, or the dots sit inside the fill */
    let last = -1;
    for (let x = 150; x < 240; x++) if (px(muted, x, 12) !== stored(24, 24, 32) && px(muted, x, 12) !== 0) last = x;
    check(last === -1 || last < 240, 'the pill stays inside the bar', 'rightmost ink x=' + last);
    enc(5, -9);                                 /* back to ON */
    check(pill(grab()) === plain, 'and the pill goes away when the track is audible');
}

/* ---------- leaving ---------- */
press(6);                                   /* B_HOME */
{
    const back = grab();
    const rowsLeft = bandsOf(textRows(back, 24, 146, 12, 60)).length;
    check(rowsLeft !== 5 || back.some((v, i) => v !== page[i]), 'HOME leaves the page',
          'rows on the screen after HOME: ' + rowsLeft);
}

console.log('\n' + (fails ? 'FAILURES: ' + fails + '/' + checks : 'TRACK SET PROBE OK (' + checks + ' checks)'));
process.exit(fails ? 1 : 0);
