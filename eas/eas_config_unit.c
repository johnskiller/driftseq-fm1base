/* SPDX-License-Identifier: Apache-2.0 */
/* Sonivox EAS's eas_config.c as its own translation unit (fm1.toml o2 list):
 *
 * WHY APART. In the static-memory model (_STATIC_MEMORY) its data-module block
 * externs eas_Data, eas_Synth, eas_MIDI, eas_MIDIData and eas_SMFData as
 * EAS_VOID_PTR (void *), while eas_data.c et al define them with their real
 * struct types. Across translation units the linker matches the symbols and
 * the type mismatch is invisible; inside one unity unit C rejects the
 * redeclaration outright. This unit keeps the original layout: the externs
 * here, the definitions in eas_unity.c.
 *
 * It also fixes the module tables the library looks its parsers and effects up
 * through: parserModules references the SMF parser unconditionally (vendored
 * code that cannot be edited), so eas_smf.c + eas_smfdata.c stay built even
 * though a live sequencer never opens a file. */
#include "eas_config.c"
