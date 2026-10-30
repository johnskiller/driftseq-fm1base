#!/usr/bin/env python3
"""The USB-MIDI packetisation, checked against the spec rather than against itself.

The emulator's worklet and the host simulator both turn a frame into packets (and
back), and the CIN is the only thing that says how long a packet is. Getting it
wrong is invisible until it is not: a receiver walks a SysEx stream by CIN, so one
wrong code shifts every frame after it.

These are the two implementations, ported line for line:

  C  usb.c:ota_wire_send      the firmware's, and plat_sysex_send's in the host
  JS web/emu/worklet.js       CIN -> bytes, the unpacking side
  JS web/emu/index.html       sendSysexIn, the packing side

Each is checked against USB-MIDI 1.0 Table 4-1, which is written out below as
CIN_BYTES. The point is that all four agree, not that any one of them looks right.

    python3 tools/sysex_cin_probe.py
"""
import sys

# USB-MIDI 1.0 §4, Table 4-1: what each code index means, and how many bytes of
# the message the packet carries.
SPEC = {
    0x0: ("misc / reserved", 0),
    0x1: ("cable events / reserved", 0),
    0x2: ("2-byte system common", 2),
    0x3: ("3-byte system common", 3),
    0x4: ("SysEx starts or continues", 3),
    0x5: ("SysEx ends, single byte", 1),
    0x6: ("SysEx ends, 2 bytes", 2),
    0x7: ("SysEx ends, 3 bytes", 3),
    0x8: ("note off", 3),
    0x9: ("note on", 3),
    0xA: ("poly pressure", 3),
    0xB: ("control change", 3),
    0xC: ("program change", 2),
    0xD: ("channel pressure", 2),
    0xE: ("pitch bend", 3),
    0xF: ("single byte", 1),
}


def pack_c(frame):
    """usb.c:ota_wire_send / plat_sysex_send, byte for byte."""
    n, i, out = len(frame), 0, []
    while i < n:
        k = 3 if n - i >= 3 else n - i
        cin = 4 if (k == 3 and i + 3 < n) else (7 if k == 3 else 4 + k)
        out.append((cin, list(frame[i:i + k])))
        i += k
    return out


def pack_js(frame):
    """index.html:sendSysexIn, the emulator's packing side."""
    i, out = 0, []
    while i < len(frame):
        k = min(3, len(frame) - i)
        ends = i + k >= len(frame)
        cin = (7 if k == 3 else 4 + k) if ends else 4
        out.append((cin, list(frame[i:i + k])))
        i += k
    return out


def unpack_js(packets):
    """worklet.js: CIN -> body bytes (CIN_BYTES is the table above)."""
    out = []
    for cin, body in packets:
        for i in range(SPEC[cin][1]):
            out.append(body[i])
    return out


def check(frame):
    c, js = pack_c(frame), pack_js(frame)
    fails = []

    # 1. the two packers must agree, packet for packet
    if c != js:
        fails.append(f"packers disagree: C {c} vs JS {js}")

    # 2. every packet's CIN must carry exactly what Table 4-1 says, and the
    #    packet must actually hold that many bytes
    for cin, body in c:
        if SPEC[cin][1] != len(body):
            fails.append(f"CIN {cin:X} ({SPEC[cin][0]}) with {len(body)} bytes, spec says {SPEC[cin][1]}")

    # 3. the CINs must be legal for a SysEx frame: 4 to start or continue, and
    #    exactly one of 5/6/7 to end it, only on the last packet. A frame short
    #    enough to fit one packet entirely (2 or 3 bytes: F0 F7, F0 xx F7) has no
    #    "start" packet at all -- Table 4-1 says 5/6/7 end the frame with their
    #    last bytes, and that is the whole frame. Both the firmware and the page
    #    already do this, and the host's parser accepts it; the only rule that
    #    holds for every length is that the LAST packet ends it and no packet in
    #    the middle does.
    cins = [cin for cin, _ in c]
    if len(c) > 1 and cins[0] != 4:
        fails.append(f"first packet is CIN {cins[0]:X}, must be 4 (a multi-packet frame starts with F0)")
    if cins[-1] not in (5, 6, 7):
        fails.append(f"last packet is CIN {cins[-1]:X}, must end the frame (5/6/7)")
    if any(x != 4 for x in cins[:-1]):
        fails.append(f"a packet before the last is CIN {cins[:-1]}, must all be 4")

    # 4. round trip: the bytes must come back unchanged, in order
    if unpack_js(c) != list(frame):
        fails.append(f"round trip lost bytes: {unpack_js(c)} != {list(frame)}")

    return fails


def legend(n):
    """The ending CIN a frame of n bytes must use: 3k+1 -> 5, 3k+2 -> 6, 3k -> 7."""
    return {0: 7, 1: 5, 2: 6}[n % 3]


def main():
    fails = 0
    cases = []
    # every length from the shortest frame (F0 F7) to three past a packet boundary,
    # which is where the ending CIN changes
    for n in range(2, 14):
        cases.append((f"frame {n} B", [0xF0] + [0x11] * (n - 2) + [0xF7]))
    cases += [
        ("MMC PLAY", [0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7]),
        ("MMC STOP", [0xF0, 0x7F, 0x7F, 0x06, 0x01, 0xF7]),
        ("identity req", [0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7]),
        ("identity reply", [0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x7D, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0xF7]),
        ("log line (48 B)", [0xF0, 0x7D, 0x4C, 0x47] + [0x61] * 43 + [0xF7]),
        ("long dump (300 B)", [0xF0, 0x7D, 0x4C, 0x43] + [0x42] * 295 + [0xF7]),
    ]
    for name, frame in cases:
        got = check(frame)
        last_cin = pack_c(frame)[-1][0]
        want = legend(len(frame))
        if last_cin != want:
            got.append(f"ending CIN is {last_cin}, a {len(frame)}-byte frame ends with {want} (n%3={len(frame) % 3})")
        if got:
            print(f"FAIL {name} ({len(frame)} B)")
            for g in got:
                print(f"       {g}")
            fails += 1
        else:
            print(f"  ok {name:20s} {len(frame):3d} B  {len(pack_c(frame))} pkt, ends CIN {last_cin}")
    print(f"\n{len(cases)} frames, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
