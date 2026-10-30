#!/usr/bin/env node
/* A SysEx frame that arrives in pieces.
 *
 * Web MIDI hands the page whatever the port got; a slow port or a BLE-style link
 * can split one F0..F7 frame across two events. index.html keeps the unfinished
 * head in sxIn and finishes it when the rest turns up. Before the SysEx work that
 * path did not exist, and the code returned early, which also threw away any
 * ordinary MIDI that followed the frame in the same event.
 *
 * The function is lifted out of the built page and run with its neighbours stubbed,
 * so what is asserted is the shipped source, not a copy of it. `sxIn` is a module
 * variable in the page; here it is declared in the same scope the function closes
 * over, so the two halves talk to each other exactly as they do in the browser.
 *
 *   node tools/emu_sysex_split_probe.mjs
 */
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const page = readFileSync(join(here, "..", "web/emu/index.html"), "utf8");

let fails = 0;
const check = (msg, ok, detail) => {
  if (ok) console.log(`  ok ${msg}${detail ? "  [" + detail + "]" : ""}`);
  else { console.log(`FAIL ${msg}${detail ? "  [" + detail + "]" : ""}`); fails++; }
};

const grab = (name) => {
  const m = page.match(new RegExp(`function ${name}\\(([^)]*)\\) \\{[\\s\\S]*?\\n\\}`));
  if (!m) { console.log(`FAIL ${name} not found in index.html`); process.exit(1); }
  return m[0].replace(new RegExp(`^function ${name}`), `function ${name}`);
};

/* everything onMidiIn leans on, replaced by recorders */
const sysex = [];          /* frames handed to sendSysexIn */
const packets = [];        /* ordinary messages packed for the wasm */
const mon = [];            /* what the log line would show */

/* sendSysexIn is left OUT of the scope on purpose: onMidiIn then binds to the
 * injected stub below and a completed frame lands in `sysex` as bytes. The packing
 * that function does has its own probe (emu_sysex_probe.mjs); this one is about
 * which frames get assembled and sent at all. */
const scope = `
let sxIn = null;
${grab("onMidiIn")}
return {
  feed: (d) => onMidiIn({ data: d }),
  pending: () => sxIn,
};`;

const run = new Function(
  "send", "PORT_USB", "sendSysexIn", "monLine", "echoHit", "midiPacket", "node", "flashKey", "ccToPanel",
  scope,
);

const api = run(
  (m) => packets.push(m.pkt),
  0,
  (f) => sysex.push([...f]),
  (tag, b) => mon.push([tag, [...b]]),
  () => false,                                  /* never treat an input as our own echo */
  (msg) => msg[0] | ((msg[1] || 0) << 8) | ((msg[2] || 0) << 16),
  {},                                           /* the page's `node` flag */
  () => {},                                     /* flashKey: a DOM light */
  () => {},                                     /* ccToPanel */
);

const hex = (a) => [...a].map((b) => b.toString(16).padStart(2, "0")).join(" ");
const reset = () => { sysex.length = 0; packets.length = 0; mon.length = 0; };

/* ---- 1. a frame cut in two, the second half finishing it ---- */
{
  reset();
  api.feed([0xf0, 0x7e, 0x7f, 0x06]);
  check("half a frame sends nothing yet", sysex.length === 0, `${sysex.length} sent`);
  check("the head is kept for the next event", api.pending() !== null,
        api.pending() ? hex(api.pending()) : "null");
  api.feed([0x01, 0xf7]);
  check("the two halves join into one frame", sysex.length === 1 && hex(sysex[0]) === "f0 7e 7f 06 01 f7",
        sysex.length ? hex(sysex[0]) : "nothing sent");
  check("nothing is left pending", api.pending() === null);
  check("the joined frame is shown once", mon.length === 1, `${mon.length} lines`);
}

/* ---- 2. a frame cut in three ---- */
{
  reset();
  api.feed([0xf0, 0x7d, 0x4c]);
  api.feed([0x47, 0x61, 0x62]);
  check("two thirds still sends nothing", sysex.length === 0);
  api.feed([0x63, 0xf7]);
  check("three pieces join into one frame",
        sysex.length === 1 && hex(sysex[0]) === "f0 7d 4c 47 61 62 63 f7",
        sysex.length ? hex(sysex[0]) : "nothing sent");
}

/* ---- 3. MIDI after the frame in the same event must still be read ---- */
/* This is the half the old early-return threw away. */
{
  reset();
  api.feed([0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7, 0x90, 60, 100]);
  check("the frame goes out", sysex.length === 1, `${sysex.length} frames`);
  check("the note-on after it is not swallowed", packets.length === 1,
        `${packets.length} packets`);
  check("and it is the note that was played",
        packets.length === 1 && packets[0] === (0x90 | (60 << 8) | (100 << 16)),
        packets.length ? packets[0].toString(16) : "none");
}

/* ---- 4. a frame that finishes, then another frame starts in the same event ---- */
{
  reset();
  api.feed([0xf0, 0x7e, 0x7f, 0x06]);
  api.feed([0x01, 0xf7, 0xf0, 0x7f, 0x7f, 0x06, 0x02, 0xf7]);
  check("both frames go out", sysex.length === 2, `${sysex.length} frames`);
  check("the first is the identity request",
        sysex.length === 2 && hex(sysex[0]) === "f0 7e 7f 06 01 f7",
        sysex.length ? hex(sysex[0]) : "none");
  check("the second is MMC PLAY",
        sysex.length === 2 && hex(sysex[1]) === "f0 7f 7f 06 02 f7",
        sysex.length > 1 ? hex(sysex[1]) : "none");
}

/* ---- 5. one whole frame in one event, which is the usual case ---- */
{
  reset();
  api.feed([0xf0, 0x7f, 0x7f, 0x06, 0x01, 0xf7]);
  check("an unsplit frame still works", sysex.length === 1 && hex(sysex[0]) === "f0 7f 7f 06 01 f7",
        sysex.length ? hex(sysex[0]) : "none");
  check("nothing pending afterwards", api.pending() === null);
}

/* ---- 6. clock inside a frame is not a data byte ---- */
/* F8 can land anywhere, including between SysEx bytes; the firmware drops clock,
 * and so must the page, or it ends up inside the frame it is assembling. */
{
  reset();
  api.feed([0xf8, 0xf0, 0x7e, 0x7f, 0xf8, 0x06, 0x01, 0xf7, 0xf8]);
  check("clock around and inside a frame does not corrupt it",
        sysex.length === 1 && hex(sysex[0]) === "f0 7e 7f 06 01 f7",
        sysex.length ? hex(sysex[0]) : "none");
}

console.log(`\n${fails ? "EXPECTATIONS FAILED" : "all passed"}`);
process.exit(fails ? 1 : 0);
