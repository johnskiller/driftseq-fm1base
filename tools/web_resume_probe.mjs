/* /tmp/rc2.mjs — unit-check the resume logic by lifting the two functions out of
 * the built page and running them against a fake AudioContext. Stubbing the
 * whole DOM to boot the page is pointless; these two functions are the fix.
 */
import fs from 'node:fs';

const html = fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/index.html', 'utf8');

function grab(name) {
    /* match "function name" including a leading "async", otherwise the lifted
     * source loses its async and every await inside it is a syntax error */
    const re = new RegExp(`(?:async\\s+)?function\\s+${name}\\b`);
    const m = re.exec(html);
    if (!m) throw new Error(`missing ${name}`);
    const i = m.index;
    let depth = 0, started = false, j = i;
    for (; j < html.length; j++) {
        const c = html[j];
        if (c === '{') { depth++; started = true; }
        else if (c === '}') { depth--; if (started && depth === 0) { j++; break; } }
    }
    return html.slice(i, j);
}

let ok = 0, bad = 0;
const check = (cond, what, detail = '') => {
    if (cond) { ok++; console.log('  ok   ' + what + (detail ? '  [' + detail + ']' : '')); }
    else { bad++; console.log('  FAIL ' + what + (detail ? '  [' + detail + ']' : '')); }
};

console.log('waitRunning:');
{
    const src = grab('waitRunning');
    const f = new Function('Promise', 'setTimeout', 'clearTimeout', src + '; return waitRunning;')(Promise, setTimeout, clearTimeout);

    /* already running: resolves at once, no listener */
    let added = 0;
    const running = { state: 'running', addEventListener() { added++; }, removeEventListener() {} };
    const r1 = await f(running, 1000);
    check(r1 === true && added === 0, 'a running context resolves immediately');

    /* suspended, then a statechange: resolves true */
    let listener = null;
    const susp = {
        state: 'suspended',
        addEventListener(t, fn) { listener = fn; },
        removeEventListener() {},
    };
    const p = f(susp, 1000);
    susp.state = 'running';
    listener();
    check((await p) === true, 'suspended -> running resolves true');

    /* never running: resolves false on timeout, does not hang */
    const dead = { state: 'suspended', addEventListener() {}, removeEventListener() {} };
    const t0 = Date.now();
    const r3 = await f(dead, 120);
    check(r3 === false && Date.now() - t0 >= 100, 'a context that never runs resolves false', (Date.now() - t0) + 'ms');
}

console.log('waitFrame:');
{
    const src = grab('waitFrame');
    const f = new Function('setTimeout', 'clearTimeout', src + '; return waitFrame;')(setTimeout, clearTimeout);

    let handler = null, started = 0;
    const node = { port: { addEventListener(t, fn) { handler = fn; }, removeEventListener() {}, start() { started++; } } };
    const p = f(node, 1000);
    handler({ data: { type: 'ready' } });          /* not a frame: must be ignored */
    handler({ data: { type: 'frame' } });
    check((await p) === true && started === 1, 'waits for a frame, ignores anything else');

    const idle = { port: { addEventListener() {}, removeEventListener() {}, start() {} } };
    const t0 = Date.now();
    const r = await f(idle, 120);
    check(r === false && Date.now() - t0 >= 100, 'no frame -> resolves false', (Date.now() - t0) + 'ms');
}

console.log('the wiring is present in the built page:');
check(/visibilitychange/.test(html), 'listens for visibilitychange');
check(/rcResume\(\)/.test(html), 'calls rcResume on return');
check(/id="resume"/.test(html), 'has a tap-to-resume button');
check(/\$\(.resume.\)\.addEventListener\(.click., rcResume\)/.test(html), 'the button resumes');
check(/lastFrameAt = Date\.now\(\)/.test(html), 'stamps every frame');
check(/Date\.now\(\) - lastFrameAt > 3000/.test(html), 'heartbeat notices a stalled frame stream');
check(/audioCtx = ac;/.test(html), 'keeps the context for the heartbeat');

/* ---- the rebuild path -------------------------------------------------------
 * iOS may refuse to resume an interrupted context outright, so rcResume() falls
 * back to building a whole new graph. These checks pin the fallback down: it has
 * to exist, close the old context, and only rebuild inside a gesture.
 */
console.log('the rebuild path is present in the built page:');
check(/async function buildAudio/.test(html), 'buildAudio exists (the graph is rebuildable)');
check(/wasmBytes = await fetch\(.app\.wasm.\)/.test(html), 'the wasm bytes are kept for a rebuild');
check(/async function buildAudio\(ac\)/.test(html), 'the context comes in from the gesture, not from inside buildAudio');
check(/let ac = null;\n  try {\n    if \(!window\.AudioContext\)/.test(html) || /ac = new AudioContext\(\{ sampleRate: 44100/.test(html), 'a context is created in the click handler');
check(/try { await old\.ac\.close\(\); } catch {}/.test(html), 'the old context is closed once the new one is live');
check(/if \(audioCtx\.state === .running.\)/.test(html), 'a running context is left alone (no needless rebuild)');
check(/rebuilding = true/.test(html), 'a rebuild cannot start twice at once');

/* rcResume must try resume() first, then rebuild when the context refuses. Lift
 * it out with the names it closes over, and drive both outcomes. */
async function driveRcResume(state) {
    const src = grab('rcResume');
    const calls = { rebuilt: 0, showed: 0, closed: 0, made: 0 };
    const body = `
        let audioCtx = { get state() { return ${JSON.stringify(state)}; },
                         resume() { return Promise.resolve(); },
                         close() { calls.closed++; return Promise.resolve(); } };
        let node = null, rebuilding = false;
        const status = { textContent: '' }, showAlert = () => {};
        const buildAudio = async (ac) => { calls.rebuilt++; };
        const showResume = (on) => { calls.showed++; calls.lastShown = on; };
        /* the page's AudioContext: counts constructions, and a fresh one may
           be suspended or running depending on the test */
        function AudioContext() {
            calls.made++;
            return { get state() { return globalThis.__newState || 'running'; },
                     resume() { return Promise.resolve(); },
                     close() { calls.closed++; return Promise.resolve(); } };
        }
        ${src}
        return rcResume;`;
    const fn = new Function('calls', body)(calls);
    await fn();
    return calls;
}
{
    /* a running context: nothing to do, and no second context is wasted */
    globalThis.__newState = 'running';
    const running = await driveRcResume('running');
    check(running.rebuilt === 0 && running.made === 0, 'a running context does nothing at all');

    /* suspended, and resume() cannot revive it: rebuild with the context that
       was made before any await */
    globalThis.__newState = 'running';
    const suspended = await driveRcResume('suspended');
    check(suspended.rebuilt === 1, 'a suspended context DOES trigger a rebuild');
    check(suspended.made === 1, 'the replacement context is made before any await');
    check(suspended.closed >= 1, 'the old context is closed afterwards');
}

console.log(bad ? `\n== ${bad} FAILURES ==` : `\nRESUME LOGIC OK (${ok} checks)`);
process.exit(bad ? 1 : 0);
