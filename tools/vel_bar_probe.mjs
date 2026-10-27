/* tools/vel_bar_probe.mjs — plan D's other half: the EDIT panel bar's LENGTH is
 * velocity now (was gate). The claim, as pixels: a voice at vel 40 draws a bar
 * of 3 + 40*231/127 = 75..76 px; one at vel 100 draws 3 + 100*231/127 = 184..185.
 * The desaturated hue against the flat card bg (22,22,27) is the boundary.
 *
 * Sequence: keybed write F#3 (step entry, cursor advances), step back, EDIT
 * layer, add A3 as voice 2, SELECT back to voice 0, K3 -30 detents (vel 100 ->
 * 40). Then read both bar ends off the framebuffer.
 */
import fs from 'node:fs';
const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();
const exp = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };

let fails = 0;
const check = (ok, label, detail = '') => { if (!ok) fails++; console.log(`${ok ? 'ok  :' : 'FAIL:'} ${label}${detail ? `  [${detail}]` : ''}`); };
function barEnd(fb, y) {
    let last = 3;
    for (let x = 3; x < 237; x++) {
        const [rr, gg, bb] = exp(fb[y * W + x]);
        const sat = Math.max(rr, gg, bb) - Math.min(rr, gg, bb);
        const v = (rr + gg + bb) / 3;
        if (sat > 18 && v > 60) last = x;
    }
    return last;
}

M.web_boot(); step(400);
M.web_buttons(1 << 9); step(80); M.web_buttons(0); step(150);
M.web_enc(0, 6); step(150);                                    // melodic track 6
M.web_keys(1 << 1); step(60); M.web_keys(0); step(60);         // F#3 at step 0
M.web_enc(3, -1); step(60);                                    // cursor back
M.web_buttons(1 << 4); step(60); M.web_buttons(0); step(80);   // EDIT on
M.web_keys(1 << 4); step(60); M.web_keys(0); step(60);         // + A3
M.web_enc(0, -1); step(60);                                    // voice 0
M.web_enc(5, -30); step(60);                                   // vel 100 -> 40
const fb = grab();
const b0 = barEnd(fb, 117), b1 = barEnd(fb, 142);
const span = (vel) => 3 + (vel * 231) / 127;
check(Math.abs(b0 - span(40)) <= 3, 'vel 40 bar is ~76px', b0 + 'px (want ' + span(40).toFixed(0) + ')');
check(Math.abs(b1 - span(100)) <= 3, 'vel 100 bar is ~185px', b1 + 'px (want ' + span(100).toFixed(0) + ')');
check(b1 - b0 > 60, 'the two bars are plainly different lengths', (b1 - b0) + 'px apart');
console.log(fails ? `\nVEL BAR FAILED (${fails})` : '\nVEL BAR PASSED');
process.exit(fails ? 1 : 0);
