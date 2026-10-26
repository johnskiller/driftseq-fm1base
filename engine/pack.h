/* SPDX-License-Identifier: MPL-2.0 */
/* pack.h — a per-track sparse serialisation of ds_track, for flash.
 *
 * John: "现在的pattern，无条件分配了64step，每个step4voice，16track而不管实际用了
 * 多少，应该有更高效的保存方法。当然malloc那一套动态分配行不通"
 * and then "至少可以按track存，没用的trk不消耗".
 *
 * The constraint that rules out malloc is worth stating precisely, because it
 * is what makes this file look the way it does: the firmware has no heap for
 * this (the BLE stack owns the only heap and it is a fixed shim), and a
 * fragmenting allocator in a 96 KiB device is a bug waiting for the 40th
 * pattern swap, not a feature. So the format must be:
 *
 *   - fixed maximum footprint per track (so worst case is still provable
 *     at compile time, and an out-of-flash write is a compile-time fact),
 *   - but only the used prefix actually written, so an EMPTY track costs one
 *     byte and a 3-note bass line costs ~50, not 1,293.
 *
 * That is a variable-length record with a hard ceiling -- not dynamic
 * allocation. Every track is length-prefixed, so a reader can skip one it does
 * not want without knowing its contents, and a truncated partition degrades to
 * "this track is missing" instead of corrupting the ones after it.
 *
 * FORMAT (all little-endian, no alignment padding -- the file is a byte stream,
 * not a struct, precisely so a compiler change cannot silently change it):
 *
 *   pattern := track_record * DS_TRACKS        (all 16 present, in order)
 *
 *   track_record := u8 flags | [u8 tflags] ... payload
 *     flags bit0 USED     : 0 means the track is at defaults; 1 byte total
 *     flags bit1 METASONLY: no steps, but track meta differs from defaults
 *     flags bit2 EUCLID   : meta carries eu_on/k/n/rot (1 byte, packed)
 *     flags bit3 FIXEDLEN : loop_len == 64, so the length byte is omitted
 *
 *   payload (when USED):
 *     u8 division | u8 loop_len | u8 loop_start | u8 channel | u8 route
 *     u8 prog | u8 mute | u8 swing | u8 voice_mode
 *     u8 nsteps                       -- number of non-empty steps that follow
 *     step_run * nsteps
 *
 *   step_run := u8 gap | u8 hdr | [voice...]
 *     gap = steps since the previous run's last step - 1   (run-length on the
 *           step axis; a drum track's four-on-floor is 4 runs of gap 3)
 *     hdr bits0-1 nvoices-1
 *         bit2    on        (en bit; 0 = silent-keep, so a run can be present
 *                            with on=0 -- it is still content)
 *         bit3    prob!=100
 *         bit4    cond!=DS_C_NONE
 *         bit5    gate!=1 (in the voice byte)
 *     then nvoices voice records:
 *       voice := u8 note | u8 vel | [u8 gate]
 *
 *   VOICE PACKING: gate is 0..63 or 0xFF, and vel is 1..127, neither fits 7
 *   bits together with a flag, so gate stays a separate optional byte. note is
 *   0..127 (7 bits) and vel is 1..127 (7 bits): together 14 bits, but a
 *   two-byte union with gate saves nothing over three plain bytes when gate is
 *   present, and only saves when it is not. Keep it simple: 2 or 3 bytes.
 *
 * The ceiling per track is therefore 1 + 9 + 1 + 64*(1+1+4*3) = 907 bytes,
 * against 1,293 today -- and the empty case is 1 byte against 1,293.
 *
 * This is a SAVE FORMAT only. The live pattern in RAM stays the flat struct:
 * the UI mutates steps by index at 40 fps, and a sparse in-RAM form would make
 * every draw a decode. Save/load happens at pattern change, which is a human
 * timescale, so the decode cost is irrelevant.
 */
#ifndef DS_PACK_H
#define DS_PACK_H

#include <stdint.h>
#include <stddef.h>
#include "driftseq.h"

/* Worst case, proved by arithmetic rather than guessed: a track with all 64
 * steps present, 4 voices each, and every optional byte set costs
 *
 *   12 B header  (flags + euclid k/n/rot + 8 meta + nsteps)
 * + 64 runs x 16 B  (gap + hdr + prob + cond + 4 x [note + vel + gate])
 * = 1,036 B
 *
 * against 1,293 B for the flat struct -- and an unused track costs 1 byte
 * against 1,293. The ceiling is what makes "do 16 patterns fit in the
 * partition" an arithmetic question at build time, not something a user finds
 * out at save time. */
#define DS_PACK_TRACK_MAX   1036u
#define DS_PACK_TRACK_EMPTY 1u
#define DS_PACK_PATTERN_MAX (DS_PACK_TRACK_MAX * DS_TRACKS)

/* write one track; returns bytes written, 0 on overflow. A record is
 * self-describing (flags + optional fields + nsteps + runs), so a reader walks
 * it forward without a separate length prefix; on overflow nothing is written
 * at all. */
size_t ds_pack_track(uint8_t *out, size_t cap, const ds_track *t);

/* read one track; returns bytes consumed, 0 on malformed input. On 0 the
 * caller must leave the track at defaults -- a bad record poisons one track,
 * never the pattern. */
size_t ds_unpack_track(const uint8_t *in, size_t len, ds_track *t);

size_t ds_pack_pattern(uint8_t *out, size_t cap, const ds_pattern *p);
size_t ds_unpack_pattern(const uint8_t *in, size_t len, ds_pattern *p);

#endif
