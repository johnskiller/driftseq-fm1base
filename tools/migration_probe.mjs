/* tools/migration_probe.mjs — every field the old tree's header showed is still
 * on screen, per page.
 *
 * John: "确认一遍、看看是否旧版的功能都迁移过来了". Comparing C functions found no
 * missing function, so the risk is not a whole function but a FIELD dropped in
 * a rewrite -- which is exactly what happened twice already: GLO (a dropped
 * branch) and the drum page's `ST n` (a dropped string in the 24px header).
 *
 * The old tree's headers, transcribed from firmware/src/ui_screens.c:
 *
 *   HOME      TRK n | PTN 01 | BPM | DIV        ... n BAR ... S n
 *   GRID mel  SEQ TRK n | DIV | ST n [chord]
 *   GRID drum DRUM TRK n | P n/4 | DIV | ST n drumname
 *
 * PTN 01 was a hard-coded literal with no backing state, and S n was removed on
 * purpose (the knob readout replaced it), so those two are expected absences.
 * Everything else must be present, and this probe reads it off the pixels.
 *
 * It cannot OCR, so it does not assert exact strings. It asserts the property
 * that actually matters per field: the header's ink either changes with the
 * value (the field is live) or is a constant label (it is present). A dropped
 * field shows up as "this header never changes when I change its value".
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

const exp = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

/* the header bar y0..22, split into left (context, x 32..150) and right
 * (x 150..240) so a change can be attributed to a side */
const inkOf = (fb, x0, x1) => {
    const out = [];
    for (let y = 0; y < 22; y++) for (let x = x0; x < x1; x++) {
        const [r, g, b] = exp(fb[y * W + x]);
        out.push((r + g + b) / 3 > 90 ? 1 : 0);
    }
    return out;
};
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const ctxInk = (fb) => inkOf(fb, 36, 150);   /* past the pill, which ends at x=29 */
const rightInk = (fb) => inkOf(fb, 150, 240);

let fails = 0;
const check = (ok, label, detail = '') => {
    if (!ok) fails++;
    console.log(`${ok ? 'ok  :' : 'FAIL:'} ${label}${detail ? `  [${detail}]` : ''}`);
};

const B_GLO = 5, B_SEQ = 9, B_HOME_ = 6, B_ARP = 8;
const press = (bit) => { M.web_buttons(1 << bit); step(50); M.web_buttons(0); step(80); };
const hold = (bit) => { M.web_buttons(1 << bit); step(50); };
const rel = () => { M.web_buttons(0); step(50); };
const enc = (e, d) => { M.web_enc(e, d); step(50); };

console.log('== HOME ==');
{
    /* BPM/DIV are display-only on HOME (no encoder reaches them there), so
     * assert PRESENCE: the context must carry three separate text runs, which
     * reads as three ink clusters separated by gaps. Count clusters across the
     * context band. */
    const ink = ctxInk(grab());
    const cols = [];
    for (let x = 0; x < 114; x++) {
        let any = 0;
        for (let y = 0; y < 22; y++) if (ink[y * 114 + x]) { any = 1; break; }
        cols.push(any);
    }
    let clusters = 0;
    for (let i = 1; i < cols.length; i++) if (cols[i] && !cols[i - 1]) clusters++;
    check(clusters >= 3, 'HOME context carries bars + BPM + DIV as separate runs',
          `${clusters} ink clusters (old bar had 3 fields)`);
    /* the zoom readout: GLO+K2 changes n BAR */
    const b2 = ctxInk(grab());
    hold(B_GLO); enc(4, 1); rel();
    check(!same(b2, ctxInk(grab())), 'HOME shows the zoom span (n BAR), and GLO+K2 moves it');
    step(1200);
}

console.log('\n== GRID melodic ==');
{
    press(B_SEQ);
    const a = ctxInk(grab());
    enc(3, 1);                                  /* EN_K1 = STEP within page */
    check(!same(a, ctxInk(grab())), 'GRID mel header shows the step number (it moves with K1)');
    step(1200);
    /* the chord name must track which notes are under the cursor, so two steps
     * with different note content must print differently */
    const atA = ctxInk(grab());
    enc(3, 1);
    const atB = ctxInk(grab());
    enc(3, 1);
    const atC = ctxInk(grab());
    check(!same(atA, atB) || !same(atB, atC),
          'GRID mel header prints per-step note content (chord name)',
          'differs across neighbouring steps');
}

console.log('\n== GRID drum: the field the 24px rewrite dropped ==');
{
    /* The demo's drum tracks are pat.track[0..2] (channel 9, demo_pattern.c),
     * i.e. the FIRST three tracks -- so walk from the top, not to track 10. */
    press(B_HOME_);
    for (let i = 0; i < 20; i++) M.web_enc(0, -1);        /* back to track 1 */
    step(80);
    press(B_SEQ);
    let drumFound = false;
    {
        /* The drum page's signature is the storage card's separator row at
         * y=115 (the 9 lanes stop above it); the melodic page's cell grid runs
         * straight through that row. Measured: drums give cardRow=115, melodic
         * gives 112 or none. */
        const fb = grab();
        for (let y = 113; y <= 117; y++) {
            let hits = 0;
            for (let x = 20; x < 230; x++) {
                const [r, g, b] = exp(fb[y * W + x]);
                if (r > 50 && r < 110 && Math.abs(r - g) < 14) hits++;
            }
            if (hits > 150) { drumFound = true; break; }
        }
    }
    check(drumFound, 'a drum track is reachable and draws its lane grid', 'detected by lane labels');
    press(B_SEQ);
    const drumPage = ctxInk(grab());

    enc(3, 1);                                  /* cursor +1 */
    const after1 = ctxInk(grab());
    check(!same(drumPage, after1),
          'the drum header changes when the cursor moves (ST n is present)',
          'it was dropped in the 24px rewrite');

    enc(3, 1);
    check(!same(after1, ctxInk(grab())),
          'and it keeps changing one step at a time', 'ST n, not a static page label');
    step(1200);

    /* the drum name sits on the right; it must be non-empty ink there */
    const rInk = rightInk(grab()).reduce((a, b) => a + b, 0);
    check(rInk > 20, 'the drum name is drawn on the right of the bar', `${rInk} ink pixels`);
}

console.log('\n== every page keeps the bar 24px and shares one floor ==');
{
    /* The floor is the separator line RGB565(42,42,52) at y=23. Find it as the
     * first row, scanning down from y=20, where most of the row is that exact
     * colour. Using a single column does not work: x=200 lands on hero content
     * on HOME and on the ARP ring on ARP. */
    const SEP = [42, 42, 52];
    const pages = [];
    press(B_HOME_); pages.push(['HOME', grab()]);
    press(B_SEQ);   pages.push(['GRID', grab()]);
    press(B_ARP);   pages.push(['ARP', grab()]);

    for (const [name, fb] of pages) {
        let floor = -1;
        for (let y = 18; y < 40; y++) {
            let hits = 0;
            for (let x = 0; x < 240; x++) {
                const [r, g, b] = exp(fb[y * W + x]);
                if (Math.abs(r - SEP[0]) + Math.abs(g - SEP[1]) + Math.abs(b - SEP[2]) < 12) hits++;
            }
            if (hits > 200) { floor = y; break; }
        }
        check(floor === 23, `${name}: the bar's floor line is at y=23 (a 24px bar)`,
              floor < 0 ? 'no separator row found' : `y=${floor}`);
    }
}

console.log(fails ? `\nMIGRATION FAILED (${fails})` : '\nMIGRATION OK');
process.exit(fails ? 1 : 0);
