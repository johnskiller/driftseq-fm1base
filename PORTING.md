# DriftSeq on fm1-base — port project

A new project (not a branch of `driftseq`) that rebuilds DriftSeq on
[fm1-base](https://github.com/charlesvestal/fm1-base) instead of on the vendored
Felucca/X0X HAL that `driftseq` carries. The reason is Bluetooth MIDI: fm1-base
gives it with `ble = true`, and fm1-base is **MPL-2.0** file by file, which
combines with JieLi's Apache-2.0 *binary* Bluetooth libraries — something the
GPL-3.0-only trees (Polly, CTL-1) cannot do.

## State

| | |
|---|---|
| template builds unchanged | **yes** — 218,928 B app, identity FM-1_1100 |
| builds with this app | **yes** — 218,928 B, identity **FM-1_9200**, BLE in parts |
| engine golden hash | **52CF520E290E322D** — unchanged, run inside this project |
| engine tests | `test_engine` ALL PASSED, `test_euclid` PASSED, `test_golden` hash matches |
| EAS compiles here | wtsynth / midi / sndlibmgt / voicemgt: **0 errors** |
| UI ported | **no** — this is the large piece (1,973 lines, see below) |
| on hardware | **no** — not flashed |

Reproduce:

```sh
# engine contracts, no toolchain needed
gcc -O2 -Iengine tests_engine/test_golden.c engine/driftseq.c engine/demo_pattern.c -lm -o /tmp/tg && /tmp/tg
# the firmware (from the fm1-ctrl checkout that has platform/ as a submodule)
AC79_SDK=/tmp/fw-AC79_AIoT_SDK JIELI_TOOLCHAIN=/home/john/.jieli/toolchain \
  python3 platform/tools/build.py /home/john/.hermes/workspace/driftseq-fm1base
```

## Layout

```
engine/         driftseq.c, driftseq.h, demo_pattern.* — GPL-3.0-only, byte-identical
                to driftseq/host/ (md5 c6d553ea5d0473e0f0a70c558b5e1dc4). The
                port must not change these; the golden hash is the proof.
tests_engine/   test_golden.c, test_engine.c, test_euclid.c — carried over untouched
eas/            Sonivox EAS + the 212 KB GM wavetable set, Apache-2.0 (from
                Polly/CTL-1). fm1inc/ REMOVED: it held a GPL-3.0-only string.h,
                replaced by eas_inc/string.h (see below).
eas_inc/        our string.h: strcmp only, static inline
app.c, app.h    currently still the template — this is where the UI port lands
fm1.toml        identity FM-1_9200, ble = true
```

## The `strcmp` question, answered by measurement

Sonivox EAS does `#include <string.h>`, and the pi32v2 toolchain has no usable C
library header: `#include <string.h>` fails even though
`pi32v2/include/string.h` exists on disk, because that copy belongs to the older
pi32 compiler. The EAS sources are third-party Apache-2.0 and must not be edited.

What EAS actually needs, counted rather than assumed:

| function | call sites | who provides it here |
|---|---|---|
| `strcmp` | 3, all in `eas_sndlibmgt.c` against its own literals | **ours** — `eas_inc/string.h`, static inline |
| `memset` | 3, `eas_wtsynth.c` | fm1-base `bsp/chip/libc.c` (MPL-2.0) |
| `memcpy` | none explicit; struct copies may lower to it | same |
| `memcmp` | none found in this drop | same |

So the replacement header declares only `strcmp`, and only because there is
nothing to resolve it against. Verified: `nm eas_sndlibmgt.o` shows **no**
`strcmp` symbol (it inlined), and the four EAS cores compile with 0 errors.

The GPL-3.0-only `fm1inc/string.h` that came with the EAS tree is deleted, not
carried. That file was the one thing standing between this project and a
distributable BLE binary.

## Licence position

**DriftSeq here is MPL-2.0**, which is the point of the port. Full reasoning in
`LICENSING.md`; the short version:

- MPL-2.0 is **per file**; §3.3 lets a Larger Work go out under other terms as
  long as the MPL files stay MPL and their source is available (it is, here).
- So an image that links JieLi's Apache-2.0 **binary** Bluetooth libraries —
  `btctrler.a`, `btstack.a`, `wl_rf_common.a`, `crypto_toolbox_Osize.a`,
  `lib_ccm_aes.a`, none of which has source anywhere — **can be distributed**.
- GPL-3.0 could not do that: §6 demands complete corresponding source for the
  whole work, and nobody can supply JieLi's. That is why CTL-1's LICENSING.md
  says a built Ctl cannot be distributed.

An earlier version of this file claimed the opposite ("MPL does NOT make a BLE
binary distributable") and was wrong; it has been corrected. fm1-base's own
LICENSING.md agrees: "a firmware made of felucca-bsp, fm1-base and its own code
may link JieLi's binary Bluetooth and Wi-Fi libraries."

The 32 vendored-Felucca files in the old tree (which credit Leo Kuroshita) were
never mine to relicense — and they are not carried here at all. fm1-base supplies
MPL-2.0 equivalents. Only DriftSeq's own files were relicensed.

## What is left, in the order the porting notes recommend

1. ~~template builds~~ (done)
2. ~~engine contract tests pass inside the new project~~ (done)
3. A trivial `app.c`: keys → notes, encoders → CC, blank screen; flash it.
4. Then hang the engine on it, then port the UI.
5. **The hard part is not the drawing API, it is the SPI budget.** A full
   240x224 frame is 107,520 B ≈ 19.6 ms of transfer, and `driftseq`'s
   `ui.c` does `cv_begin(240,224)` every frame. 1,973 lines of UI will not fit
   in what is left of a frame; it needs `cv_band` + `cv_blit_from` dirty regions.
6. Do not move `ds_tick` into `app_render` *yet*. With no synth, hanging musical
   time on a silent audio path couples tempo to a codec. When the EAS synth
   lands, this inverts and the tick moves into `app_render` — plan the call site
   so that is one line, not a rewrite.

## Identity

`id_prefix + "00"` is the package identity, so `FM-1_92` → `FM-1_9200`. DriftSeq's
old `FELUCCA_ID` was `FM-1_91001`, six characters after the underscore, which no
`id_prefix` can produce — that string does **not** survive this port and cannot.
A mismatch is refused by the loader (refused flash, not a brick); a stale one is
what bricks. `FM-1_92` is chosen clear of fm1-base's `11` and fm1-ctrl's `91`.
