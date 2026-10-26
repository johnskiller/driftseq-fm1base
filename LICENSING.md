# Licensing

**DriftSeq is MPL-2.0**, file by file: every source file carries
`SPDX-License-Identifier: MPL-2.0`. Full text in `LICENSE`.

## Why MPL-2.0 and not GPL-3.0

This is the whole point of the port. A built FM-1 image links JieLi's Bluetooth
libraries, which are **Apache-2.0 and binary only** — there is no source for
`btctrler.a`, `btstack.a`, `wl_rf_common.a`, `crypto_toolbox_Osize.a` or
`lib_ccm_aes.a` anywhere (verified: the AC79 SDK ships the `.a` files and zero
matching `.c`).

- Under **GPL-3.0**, the finished image is one work, and §6 demands the complete
  corresponding source for all of it. JieLi's source cannot be supplied by
  anyone, so a GPL-3.0 build could never be distributed. This is exactly why
  CTL-1's LICENSING.md says a built Ctl cannot be distributed.
- Under **MPL-2.0**, copyleft is per file. §3.3 explicitly permits distributing
  a Larger Work under other terms, provided the MPL-2.0 files stay MPL-2.0 and
  their source is made available — which it is, in this repository. Linking
  Apache-2.0 binaries into that Larger Work is fine.

So MPL-2.0 is what makes a **BLE-capable binary distributable**. It is not a
cosmetic licence change; it is the reason this project exists. fm1-base's own
LICENSING.md states the same conclusion: "a firmware made of felucca-bsp,
fm1-base and its own code may link JieLi's binary Bluetooth and Wi-Fi libraries."

## Where the code comes from

| Code | Licence | Notes |
| --- | --- | --- |
| `app.c`, `app.h`, `fm1.toml`, `words.tsv` | MPL-2.0 | from fm1-base's `template/` |
| `platform/` (submodule) | MPL-2.0 | fm1-base, **unmodified** — no publish obligation triggers |
| `engine/`, `tests_engine/` | MPL-2.0 | DriftSeq's own engine, relicensed from GPL-3.0-only |
| `eas/` | Apache-2.0 | Sonivox EAS, Copyright Sonic Network Inc. 2004-2009. `LICENSES/Apache-2.0-EAS.txt`, `eas/NOTICE` |
| `eas_inc/string.h` | MPL-2.0 | DriftSeq's own: `strcmp` only, replaces a GPL-3.0-only header that shipped inside the EAS tree |
| felucca-bsp (via `platform/`) | MPL-2.0 | Leo Kuroshita's code, used under MPL-2.0 by his permission: [hugelton/felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4) |

## What was relicensed, and why that was possible

DriftSeq's earlier tree was GPL-3.0-only. It split cleanly in two:

1. **Vendored HAL from Felucca** (32 files: `firmware/hal/*`, `lcd.c`, `gfx.c`,
   `libc.c`, `panel.c`, `usb.c`, `ota.c`, `loader/*`, `crt0.S`, `app.ld`) — these
   headers credit Leo Kuroshita / Hügelton Instruments, so they are **not mine to
   relicense**. They are also **not carried into this project at all**: fm1-base
   supplies its own MPL-2.0 equivalents, licensed that way by their author. The
   port deletes the problem instead of arguing with it.

2. **DriftSeq's own code** (engine, UI painters, map/leds/keyscan/encoders,
   midi_io) — every commit is by johnskiller, and the UI headers record their
   real provenance (`Ported from web/emu.js r16j`), which is DriftSeq's own file.
   Relicensing these to MPL-2.0 is the author's own decision.

No third-party GPL code remains anywhere in the built image.

## Not part of the firmware

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors) | SIL OFL 1.1 | via fm1-base `assets/fonts/`, with `Barlow-OFL.txt` |
| JieLi AC79 SDK files a package needs | Apache-2.0 | read from your SDK checkout at build time; never in this repository |
| JieLi's Bluetooth libraries (binary only) | Apache-2.0 | copied from your SDK checkout into `build/`; never in this repository |
| eSpeak NG, the browser emulator's voice, via fm1-reader | GPL-3.0-or-later | **optional, not bundled** — the build runs without it and the page falls back to the system screen reader |
| DriftSeq's old `web/` emulator | GPL-3.0 | stays in the old repository; not part of this firmware |

The browser emulator is a **separate program**. GPL components in it (the voice)
are loaded at runtime by the page, not linked into the app wasm, so they do not
affect the firmware image's licence. See fm1-base LICENSING.md for the same
arrangement.

## If you distribute a build

Keep these three things together with the binary:

1. The source of this repository, under MPL-2.0 (that satisfies MPL's per-file
   source obligation).
2. `LICENSES/Apache-2.0-EAS.txt` and `eas/NOTICE` (Apache-2.0 §4 requires the
   NOTICE to travel with the work).
3. Note that the image links JieLi's Apache-2.0 Bluetooth binaries, obtained from
   the AC79 SDK.

You do **not** need to publish anything from `platform/`, because it is
unmodified.
