/* tools/emu_dom_probe.mjs — the emulator page's own script, run in a DOM.
 *
 * The emulator's AudioWorklet half needs a real browser, but everything around
 * it -- the focus rules, the shortcut handler, the two Web MIDI pickers, the
 * mapping list -- is ordinary DOM code, and that is where the last round of bugs
 * lived: a shortcut that only worked after a click had landed on a key, and a
 * "Keyboard shortcuts" checkbox with nothing under it. jsdom runs the BUILT page
 * (build/emu/index.html), so what is asserted is the shipped file.
 *
 *   npm i jsdom          # once, anywhere this can resolve it
 *   node tools/emu_dom_probe.mjs
 *
 * Two of the checks are known to fail when the fix they cover is reverted (the
 * shortcut listener moved back to #controls: 4 checks; the focus fallback
 * removed: 2), which is the only reason to trust the rest.
 */
import fs from 'node:fs';
import { createRequire } from 'node:module';

/* jsdom is not a dependency of this repository (the other probes need nothing
 * but node), so it is looked up where a one-off `npm i jsdom` would have put it:
 * beside this file, in the workspace, or in the home directory. */
const jsdom = (() => {
    const need = createRequire(import.meta.url);
    const where = ['', process.cwd() + '/', process.env.HOME + '/'];
    for (const w of where) {
        for (const m of ['jsdom']) {
            try { return need(w + 'node_modules/' + m); } catch { /* the next */ }
        }
    }
    try { return need('jsdom'); } catch { /* nothing */ }
    console.error('emu_dom_probe: jsdom is not installed. Run `npm i jsdom` in this directory (or anywhere, and set\n'
        + 'NODE_PATH is not enough for ESM: put it in ~/node_modules), then run this again. The other probes --\n'
        + 'midi_probe, worklet_midi_probe, ui_probe -- need nothing extra.');
    process.exit(2);
})();
const { JSDOM, VirtualConsole } = jsdom;

const PAGE = new URL('../build/emu/index.html', import.meta.url).pathname;

let checks = 0, fails = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
};

/* A Web MIDI stand-in: ports the test can add, and a way to see what was sent. */
const ports = { ins: [], outs: [] };
function makeAccess() {
    const inputs = new Map(), outputs = new Map();
    for (const p of ports.ins) inputs.set(p.id, { id: p.id, name: p.name, onmidimessage: null });
    for (const p of ports.outs) outputs.set(p.id, { id: p.id, name: p.name, sent: [], send(d) { this.sent.push([...d]); } });
    return { inputs, outputs, onstatechange: null };
}
let access = makeAccess();

const vc = new VirtualConsole();
vc.on('jsdomError', (e) => console.log('  jsdomError: ' + e.message));
vc.on('error', (m) => console.log('  page error: ' + m));

const dom = new JSDOM(fs.readFileSync(PAGE, 'utf8'), {
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    url: 'http://127.0.0.1:8933/',
    virtualConsole: vc,
    beforeParse(win) {
        // nothing here plays audio: switchOn is never reached, and the rest of
        // the page only checks that these exist
        win.AudioContext = class {
            constructor() { this.state = 'suspended'; this.sampleRate = 44100; this.destination = {}; }
            resume() { return Promise.resolve(); }
            close() { return Promise.resolve(); }
            createGain() { return { gain: { value: 1 }, connect() { return this; } }; }
            createOscillator() { return { frequency: { value: 0 }, type: '', connect() { return this; }, start() {}, stop() {}, disconnect() {} }; }
        };
        win.navigator.requestMIDIAccess = () => Promise.resolve(access);
        win.ResizeObserver = class { observe() {} disconnect() {} };
        // jsdom has no layout, so no SVG box model either: hit() sizes every
        // control from getBBox(). Give each shape a small box at its own x/y, which
        // is all the page needs to place the invisible HTML targets.
        win.SVGElement.prototype.getBBox = function () {
            const n = (a, d) => { const v = parseFloat(this.getAttribute(a)); return Number.isFinite(v) ? v : d; };
            const w = n('width', this.tagName === 'circle' ? 2 * n('r', 10) : 20);
            const h = n('height', this.tagName === 'circle' ? 2 * n('r', 10) : 20);
            const x = this.tagName === 'circle' ? n('cx', 0) - n('r', 10) : n('x', 0);
            const y = this.tagName === 'circle' ? n('cy', 0) - n('r', 10) : n('y', 0);
            return { x, y, width: w, height: h, left: x, top: y, right: x + w, bottom: y + h };
        };
        // the LCD is drawn through a 2d context; jsdom has none, and a null one
        // aborts the whole script at the first createImageData
        const ctx2d = () => ({
            imageSmoothingEnabled: false, canvas: null,
            createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
            putImageData() {}, drawImage() {},
        });
        win.HTMLCanvasElement.prototype.getContext = function () { const c = ctx2d(); c.canvas = this; return c; };
        if (!win.matchMedia) win.matchMedia = () => ({ matches: false, addEventListener() {}, removeEventListener() {} });
    },
});
const win = dom.window, doc = win.document;
await new Promise((r) => win.addEventListener('load', r, { once: true }));

/* ---------------------------------------------------------------- structure */
const ins = doc.getElementById('midiins'), outs = doc.getElementById('midiouts');
check(!!ins && !!outs, 'the bar has an in and an out picker');
check(ins.disabled && outs.disabled, 'both start disabled (there is no device yet)');
const rows = [...doc.getElementById('keys-list').querySelectorAll('dt')].map((d) => d.textContent.trim());
check(rows.length >= 8, 'the mapping list is filled from the keymap', rows.length + ' rows');
const rowText = rows.join(' | ');
check(/(^| )Q( |$)/.test(rowText) && /(^| )P( |$)/.test(rowText) && /Esc/.test(rowText) && /↑/.test(rowText),
      'it lists the piano rows, the buttons and the arrow keys', rowText);
check(!!doc.getElementById('keys-help') && doc.getElementById('keys-help').tagName === 'DETAILS',
      'the list lives in a details, under the shortcuts checkbox');
const dds = [...doc.getElementById('keys-list').querySelectorAll('dd')].map((d) => d.textContent);
check(dds.every((s) => s && s.trim().length), 'every row says what its key does', dds[0]);

/* ---------------------------------------------------------------- focus */
/* The shortcut handler used to listen on #controls, so a click on the screen or
 * the case left the focus on <body> and nothing fired. */
const device = doc.getElementById('device'), controls = doc.getElementById('controls');
controls.inert = false;                       /* as buildAudio leaves it */
const hits = [...controls.querySelectorAll('.hit')];
hits[0].focus();
check(doc.activeElement === hits[0], 'the panel can hold the focus');

/* jsdom has no layout, so there is no hit-test: fire the mousedown a click on the
 * case produces, with the device itself as the target */
const clickCase = () => {
    const e = new win.MouseEvent('mousedown', { bubbles: true, cancelable: true });
    Object.defineProperty(e, 'target', { value: device });
    device.dispatchEvent(e);
    return e;
};
const e0 = clickCase();
check(doc.activeElement === hits[0], 'a click on the case returns the focus to the last control used',
      doc.activeElement && (doc.activeElement.className || doc.activeElement.tagName));
check(e0.defaultPrevented, 'and it stops the browser taking the focus away first');

const other = hits[5];
other.focus();
/* the browser's own move on mousedown: the focus falls away from the panel,
 * which is exactly the state that used to make the keyboard look dead */
doc.activeElement.blur();
check(!controls.contains(doc.activeElement), 'the focus starts out outside the panel',
      doc.activeElement && (doc.activeElement.tagName));
const e1 = clickCase();
check(doc.activeElement === other, 'a click on the case returns it to the control used last',
      doc.activeElement && (doc.activeElement.className || doc.activeElement.tagName));

/* a click on a control is that control's, not the fallback's */
const hitEl = hits[3];
hitEl.focus();
const e2 = new win.MouseEvent('mousedown', { bubbles: true, cancelable: true });
Object.defineProperty(e2, 'target', { value: hitEl });
device.dispatchEvent(e2);
check(doc.activeElement === hitEl && !e2.defaultPrevented, 'a click on a control is left to the control');

/* ---------------------------------------------------------------- shortcuts */
const key = (code) => {
    const e = new win.KeyboardEvent('keydown', { bubbles: true, cancelable: true });
    Object.defineProperty(e, 'code', { value: code });
    doc.body.dispatchEvent(e);
    return e;
};
/* With no worklet there is no `node`, and every shortcut returns early: the page
 * must not swallow keys before it is switched on. */
check(key('KeyQ').defaultPrevented === false, 'before switching on, a letter is not swallowed');

const box = doc.getElementById('shortcuts');
check(box.checked, 'shortcuts are on by default');
box.checked = false; box.dispatchEvent(new win.Event('change'));
check(key('KeyQ').defaultPrevented === false, 'with the checkbox off, a letter is not swallowed');
box.checked = true; box.dispatchEvent(new win.Event('change'));

const input = doc.createElement('input');
input.type = 'text';
doc.body.appendChild(input);
check(key('KeyQ').defaultPrevented === false, 'a letter typed in a text field is not swallowed');

/* Now the real thing: a fake worklet node, so `node` exists and the handler runs.
 * A key press must reach the wasm as a key mask, from <body> -- not only from a
 * focused control. */
const sent = [];
win.eval('node = { port: { postMessage: (m) => { window.__sent.push(m); } } }; window.__sent = [];');
win.__sent.length = 0;
const kq = key('KeyQ');
check(kq.defaultPrevented, 'with the panel live, a letter is taken (it is a shortcut now)',
      'defaultPrevented=' + kq.defaultPrevented);
check(win.__sent.some((m) => m.type === 'keys' && m.mask === 1), 'q presses white key 1 (F3)',
      JSON.stringify(win.__sent.filter((m) => m.type === 'keys')));
const up = new win.KeyboardEvent('keyup', { bubbles: true });
Object.defineProperty(up, 'code', { value: 'KeyQ' });
doc.body.dispatchEvent(up);
/* a press from the keyboard is held for at least 50 ms: the firmware reads the
 * panel every 16 ms and a tap between two reads would be lost */
check(win.__sent.filter((m) => m.type === 'keys' && m.mask === 0).length === 0,
      'the release waits its 50 ms, so the firmware sees the press');
await new Promise((r) => setTimeout(r, 90));
check(win.__sent.some((m) => m.type === 'keys' && m.mask === 0), 'then letting go releases it',
      JSON.stringify(win.__sent.filter((m) => m.type === 'keys')));

win.__sent.length = 0;
key('KeyP');
await new Promise((r) => setTimeout(r, 10));
check(win.__sent.some((m) => m.type === 'buttons' && m.mask === (1 << 10)), 'p presses PLAY',
      JSON.stringify(win.__sent.filter((m) => m.type === 'buttons')));
const upP = new win.KeyboardEvent('keyup', { bubbles: true });
Object.defineProperty(upP, 'code', { value: 'KeyP' });
doc.body.dispatchEvent(upP);
await new Promise((r) => setTimeout(r, 90));
check(win.__sent.some((m) => m.type === 'buttons' && m.mask === 0), 'and p releases it',
      JSON.stringify(win.__sent.filter((m) => m.type === 'buttons')));

/* ---------------------------------------------------------------- MIDI */
/* The pickers fill on a state change, and a port that appears later shows up.
 * midiStart() is only reachable after a successful switch-on, so it is called
 * directly through the page's global -- a classic script's top-level function
 * declarations are window properties. */
ports.ins.push({ id: 'in-1', name: 'FM-1 Bluetooth' }, { id: 'in-2', name: 'KeyStep' });
ports.outs.push({ id: 'out-1', name: 'MU90' });
access = makeAccess();
await win.eval('midiStart()');

const optText = (sel) => [...sel.options].map((o) => o.textContent);
check(optText(ins).includes('FM-1 Bluetooth') && optText(ins).includes('KeyStep'), 'both inputs are listed', optText(ins).join(' / '));
check(/all inputs/.test(optText(ins)[0]), 'the first input entry is "all inputs"', optText(ins)[0]);
check(optText(outs).includes('MU90'), 'the output is listed', optText(outs).join(' / '));
check(/none/.test(optText(outs)[0]), 'the first output entry plays locally', optText(outs)[0]);
check(!ins.disabled && !outs.disabled, 'both pickers are enabled once ports exist');
check(/2 in, 1 out/.test(doc.getElementById('midi-state').textContent), 'the state line counts them',
      doc.getElementById('midi-state').textContent);

/* a port hot-plugged in shows up on the access's own statechange */
check(typeof access.onstatechange === 'function', 'a state change is subscribed to');
const third = { id: 'in-3', name: 'Keystation', onmidimessage: null };
access.inputs.set(third.id, third);
access.onstatechange();
check(optText(ins).includes('Keystation'), 'a port plugged in later appears', optText(ins).join(' / '));
check(/3 in, 1 out/.test(doc.getElementById('midi-state').textContent), 'the count follows',
      doc.getElementById('midi-state').textContent);

/* every input listens while "all inputs" is selected ... */
const handlers = [...access.inputs.values()].filter((p) => typeof p.onmidimessage === 'function');
check(handlers.length === 3, 'all three inputs are listened to', handlers.length + ' listening');

/* ... and picking one leaves the others alone */
ins.value = 'in-2';
ins.dispatchEvent(new win.Event('change'));
const listening = [...access.inputs.values()].filter((p) => typeof p.onmidimessage === 'function');
check(listening.length === 1 && listening[0].id === 'in-2', 'choosing one input detaches the others',
      listening.map((p) => p.id).join(','));

/* a note from the selected input reaches the worklet as a USB-MIDI packet */
win.__sent.length = 0;
listening[0].onmidimessage({ data: new Uint8Array([0x90, 60, 100]) });
const midiMsg = win.__sent.filter((m) => m.type === 'midi');
check(midiMsg.length === 1, 'a note-on is handed to the worklet', JSON.stringify(midiMsg));
check(midiMsg[0] && midiMsg[0].port === 0, 'it comes in on PORT_USB', midiMsg[0] && midiMsg[0].port);
check(midiMsg[0] && midiMsg[0].pkt === 0x643c9009, 'packed as CIN 9 | 90 3C 64', midiMsg[0] && midiMsg[0].pkt.toString(16));

win.__sent.length = 0;
listening[0].onmidimessage({ data: new Uint8Array([0x80, 60, 0]) });
check(win.__sent.some((m) => m.type === 'midi' && m.pkt === 0x003c8008), 'a note-off too',
      JSON.stringify(win.__sent.filter((m) => m.type === 'midi').map((m) => m.pkt.toString(16))));

/* a real-time byte interleaved inside a message must not shift the rest: the
 * page walks the buffer instead of assuming three bytes are one note */
win.__sent.length = 0;
listening[0].onmidimessage({ data: new Uint8Array([0xf8, 0x90, 62, 0xf8, 100]) });
const walk = win.__sent.filter((m) => m.type === 'midi');
check(walk.length === 1 && walk[0].pkt === 0x643E9009, 'clock bytes inside a note are skipped, not parsed as data',
      walk.map((m) => m.pkt.toString(16)).join(','));

/* program change is 2 bytes: the next status must not be eaten */
win.__sent.length = 0;
listening[0].onmidimessage({ data: new Uint8Array([0xc0, 5, 0x90, 64, 90]) });
const two = win.__sent.filter((m) => m.type === 'midi');
check(two.length === 2 && two[1].pkt === 0x5A409009, 'a program change followed by a note gives two messages',
      two.map((m) => m.pkt.toString(16)).join(','));

/* SysEx is the screen reader's channel on this page and must not be forwarded */
win.__sent.length = 0;
listening[0].onmidimessage({ data: new Uint8Array([0xf0, 0x7d, 0x53, 0x52, 0xf7]) });
check(win.__sent.filter((m) => m.type === 'midi').length === 0, 'SysEx from a keyboard is not forwarded');

/* what the app sends goes out of the chosen port */
outs.value = 'out-1';
outs.dispatchEvent(new win.Event('change'));
const port = access.outputs.get('out-1');
check(win.eval('midiOutSel') === 'out-1', 'the output selection is remembered', win.eval('midiOutSel'));
win.eval('onMidiOut(new Uint8Array([0x90, 60, 100, 0x80, 60, 0]))');
check(port.sent.length === 1 && port.sent[0].join(',') === '144,60,100,128,60,0',
      'the app\'s bytes are sent to the chosen output', JSON.stringify(port.sent));

console.log(fails ? `EMU DOM: ${fails} FAILED of ${checks}` : `EMU DOM PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
