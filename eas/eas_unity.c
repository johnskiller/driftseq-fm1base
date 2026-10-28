/* SPDX-License-Identifier: Apache-2.0 */
/* The Sonivox EAS wavetable synth, one translation unit (an o2 unit, fm1.toml),
 * after the pattern of the app's own unity build: these are third-party files
 * that must not be edited, so the unity include here is the only way to build
 * them without their own build system, and the o2 list is how an app ships a
 * separate unit. Order matters (data before users, as in eas_public.c).
 *
 * Included, in this order (eas_config.c is NOT here: its static-model externs
 * type eas_Data and friends as void*, which cannot coexist with the real
 * definitions inside one unit -- it is its own o2 unit, eas_config_unit.c):
 *   eas_host_fm1.c   the host shim: one fixed pool in .pool (EAS_POOL_BYTES),
 *                    no files, silent reports -- the FM-1's, already here
 *   eas_data.c       eas_Data / eas_Synth / eas_MIDI statics
 *   eas_math.c       sin/cos/exp tables
 *   eas_mididata.c eas_midi.c      the live MIDI stream parser
 *   eas_smfdata.c eas_smf.c        the SMF parser (see eas_config_unit.c)
 *   eas_sndlibmgt.c                loads easlib_wt_200k_g by name (strcmp!)
 *   wt_200k_G.c                    banks/programs; pulls wt_22khz.c (LFO shape)
 *                                  and wt_200k_samples.c (207 KB, XIP)
 *   eas_voicemgt.c eas_wtsynth.c eas_wtengine.c   the WT synth core
 *   eas_mixer.c eas_mixbuf.c eas_pan.c            the mix engine
 *   eas_public.c                   the API (EAS_Init / EAS_OpenMIDIStream /
 *                                  EAS_WriteMIDIStream / EAS_Render)
 *
 * Not included (their code only enters behind options eas_options.h leaves
 * off): the DLS/SF2/file-format parsers. eas_reverb.c/eas_chorus.c ARE built
 * (eas_config.c's effects table references EAS_Reverb/EAS_Chorus
 * unconditionally), and eas_filter.c too -- eas_wtengine.c calls its
 * WT_VoiceFilter for every filtered voice. eas_options.h keeps reverb and
 * chorus OFF in the mixer path; the table entries just point at the modules.
 *
 * MAX_SMF_STREAMS 4 (default 128): a stream needs ~24 B, 128 of them is 3 KB of
 * .bss for a parser a live sequencer never opens. */
#define MAX_SMF_STREAMS 4
#define _STATIC_MEMORY 1
#define MAX_SYNTH_VOICES 32

#include "eas_host_fm1.c"
#include "eas_data.c"
#include "eas_math.c"
#include "eas_mididata.c"
#include "eas_midi.c"
#include "eas_smfdata.c"
#include "eas_smf.c"
#include "eas_sndlibmgt.c"
/* wt_200k_G.c is NOT here: it defines easlib_wt_200k_g as `const S_EAS` while
 * eas_sndlibmgt.c externs it as plain S_EAS -- another cross-TU-only alias,
 * so the sound library is its own o2 unit (eas_wtlib_unit.c). */
#include "eas_voicemgt.c"
#include "eas_wtsynth.c"
#include "eas_wtengine.c"
#include "eas_filter.c"                      /* WT_VoiceFilter: eas_wtengine.c calls it */
#include "eas_reverb.c"
#include "eas_reverbdata.c"
#include "eas_chorus.c"
#include "eas_chorusdata.c"
#include "eas_pcm.c"                         /* EAS_PEInit: EAS_Init calls it (the PCM engine) */
#include "eas_mixer.c"
#include "eas_mixbuf.c"
#include "eas_pan.c"
#include "eas_public.c"
