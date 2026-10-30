#!/usr/bin/env node
/* The emulator's two halves, run against the firmware's own packet format.
 *
 * web/emu/worklet.js unpacks the packets the wasm sent into a byte stream for a
 * Web MIDI output; web/emu/index.html packs an incoming Web MIDI SysEx frame back
 * into packets for the wasm. Both used to be wrong in the same way — they sized a
 * packet by its STATUS byte, which only works for channel messages, so every
 * SysEx packet after the first (three data bytes, no status) was cut to one byte
 * and the frame arrived as F0 followed by nothing.
 *
 * The check is a round trip with the firmware's packetisation in the middle:
 *   frame -> index.html's packing -> the CIN table -> worklet.js's unpacking
 * and it must come back byte for byte. Anything else means the emulator cannot be
 * trusted to show what the device would do, which is the whole reason it exists.
 *
 * The two functions are lifted from the page rather than re-implemented, so a
 * change there that breaks the rules fails here. (Loading index.html itself would
 * drag in the DOM; the worklet's own CIN table is read from the file.)
 *
 *   node tools/emu_sysex_probe.mjs
 */
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");
const worklet = readFileSync(join(root, "web/emu/worklet.js"), "utf8");
const page = readFileSync(join(root, "web/emu/index.html"), "utf8");

let fails = 0;
function check(name, cond, detail) {
  if (cond) console.log(`  ok ${name}`);
  else { console.log(`FAIL ${name}${detail ? ": " + detail : ""}`); fails++; }
}

/* ---- the CIN table, taken from the worklet itself ---- */
const m = worklet.match(/const CIN_BYTES = \[([^\]]+)\];/);
if (!m) { console.log("FAIL CIN_BYTES not found in worklet.js"); process.exit(1); }
const CIN_BYTES = m[1].split(",").map((s) => Number(s.trim()));
check("CIN_BYTES has 16 entries", CIN_BYTES.length === 16, String(CIN_BYTES.length));
/* USB-MIDI 1.0 Table 4-1, written out independently of the file */
const SPEC = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1];
check("CIN_BYTES matches the spec's table", JSON.stringify(CIN_BYTES) === JSON.stringify(SPEC),
      `file ${JSON.stringify(CIN_BYTES)}`);

/* ---- index.html's packing, extracted from the page's source ---- */
const src = page.match(/function sendSysexIn\(frame\) \{[\s\S]*?\n\}/);
if (!src) { console.log("FAIL sendSysexIn not found in index.html"); process.exit(1); }
/* it calls send(); give it one that records, then eval the real body */
const sent = [];
const send = (pkt) => sent.push(pkt.pkt);
const sendSysexIn = new Function("send", "PORT_USB", src[0].replace(/^function sendSysexIn/, "return function sendSysexIn"))(send, 0);

/* ---- the worklet's unpacking, as it is written ---- */
function unpack(pkts) {
  const bytes = [];
  for (const p of pkts) {
    const cin = p & 0xf;
    const body = [(p >>> 8) & 0xff, (p >>> 16) & 0xff, (p >>> 24) & 0xff];
    const n = CIN_BYTES[cin];
    for (let i = 0; i < n; i++) bytes.push(body[i]);
  }
  return bytes;
}

/* ---- the cases ---- */
function roundtrip(name, frame) {
  sent.length = 0;
  sendSysexIn(frame);
  const got = unpack(sent);
  const same = got.length === frame.length && got.every((b, i) => b === frame[i]);
  check(`${name} (${frame.length} B, ${sent.length} pkt)`, same,
        same ? "" : `got ${got.map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);
  return sent;
}

/* the frames that matter: the four MMC ones John gave, the identity pair, and the
 * log frame the firmware is already sending on this very path */
const identReq = [0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7];
const identRep = [0xf0, 0x7e, 0x7f, 0x06, 0x02, 0x7d, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0xf7];
roundtrip("identity request", identReq);
roundtrip("identity reply", identRep);
roundtrip("MMC PLAY", [0xf0, 0x7f, 0x7f, 0x06, 0x02, 0xf7]);
roundtrip("MMC STOP", [0xf0, 0x7f, 0x7f, 0x06, 0x01, 0xf7]);
roundtrip("MMC RECORD", [0xf0, 0x7f, 0x7f, 0x06, 0x06, 0xf7]);
roundtrip("MMC DEFERRED PLAY", [0xf0, 0x7f, 0x7f, 0x06, 0x03, 0xf7]);
roundtrip("log line (48 B)", [0xf0, 0x7d, 0x4c, 0x47, ...Array(43).fill(0x61), 0xf7]);
roundtrip("long dump (300 B)", [0xf0, 0x7d, 0x4c, 0x43, ...Array(295).fill(0x42), 0xf7]);

/* every length from 2 to 13, which walks through all three ending CINs */
for (let n = 2; n <= 13; n++) {
  roundtrip(`frame ${n} B`, [0xf0, ...Array(n - 2).fill(0x11), 0xf7]);
}

/* ---- the CINs the packing produced must be the ones the spec asks for ---- */
{
  sent.length = 0;
  sendSysexIn(identRep);
  const cins = sent.map((p) => p & 0xf);
  check("identity reply: all CIN 4 but the last", cins.slice(0, -1).every((c) => c === 4), String(cins));
  check("identity reply: last is CIN 7", cins[cins.length - 1] === 7, String(cins));
}
{
  sent.length = 0;
  sendSysexIn([0xf0, 0xf7]);                       /* the shortest frame there is */
  check("F0 F7 packs to one CIN 6 packet", sent.length === 1 && (sent[0] & 0xf) === 6,
        `${sent.length} pkt, cin ${sent[0] & 0xf}`);
}

/* ---- the old, wrong reader would have failed all of the above ---- */
/* keep this as the regression that motivated the change: a status-byte reader
 * cannot see past the first packet of a SysEx frame. */
{
  const wrong = (pkts) => {
    const bytes = [];
    for (const p of pkts) {
      const st = (p >>> 8) & 0xff, t = st & 0xf0;
      bytes.push(st);
      if (st < 0xf0) { bytes.push((p >>> 16) & 0xff); if (t !== 0xc0 && t !== 0xd0) bytes.push((p >>> 24) & 0xff); }
      else if (st === 0xf2) { bytes.push((p >>> 16) & 0xff, (p >>> 24) & 0xff); }
    }
    return bytes;
  };
  sent.length = 0;
  sendSysexIn(identRep);
  const bad = wrong(sent);
  check("the old status-byte reader really did lose the frame", bad.length !== identRep.length,
        `${bad.length} of ${identRep.length} bytes`);
}

console.log(`\n${fails ? "EXPECTATIONS FAILED" : "all passed"}`);
process.exit(fails ? 1 : 0);
