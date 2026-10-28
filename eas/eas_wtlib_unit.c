/* SPDX-License-Identifier: Apache-2.0 */
/* The GM sound library (wt_200k_G.c) as its own translation unit (fm1.toml
 * o2 list):
 *
 * WHY APART. eas_sndlibmgt.c externs easlib_wt_200k_g as plain `S_EAS` while
 * the definition here is `const S_EAS`. Across translation units the linker
 * matches the one symbol and the qualifier difference is invisible; inside
 * one unity unit C rejects the redeclaration. Same pattern as
 * eas_config_unit.c.
 *
 * wt_200k_G.c pulls in wt_22khz.c (the LFO shape table, by _SAMPLE_RATE_22050)
 * and wt_200k_samples.c (the 207 KB sample pool, XIP) by #include itself. */
#include "wt_200k_G.c"
