/* SPDX-License-Identifier: MPL-2.0 */
/* <string.h> for the EAS sources, on fm1-base.
 *
 * WHY THIS FILE EXISTS. The pi32v2 toolchain's clang cannot find a C library
 * header at all: `#include <string.h>` fails even though the toolchain ships
 * pi32v2/include/string.h, because that copy belongs to the older pi32 compiler
 * (`clang -target pi32v2 -print-search-dirs` reports only lib/clang/4.0.1). The
 * Sonivox EAS sources are third-party Apache-2.0 code that must not be edited,
 * so their `#include <string.h>` needs to resolve to something.
 *
 * WHY ONLY strcmp. Everything else the EAS sources need is already MPL-2.0 in
 * the platform: bsp/chip/libc.c defines memset, memcpy and memcmp, and the
 * linker finds them. strcmp is the one function with no definition anywhere —
 * eas_sndlibmgt.c calls it three times, always against its own string literals
 * (`strcmp(libraryName, DEFAULT_WT_SNDLIB)` and friends). Defining it as a
 * static inline means no external symbol and nothing to resolve.
 *
 * WHY MPL HERE, NOT GPL. DriftSeq is being relicensed to MPL-2.0 file by file
 * so that a built image may link JieLi's Apache-2.0 *binary* Bluetooth
 * libraries and still be distributed. MPL-2.0 §3.3 permits a Larger Work under
 * other terms so long as the MPL files stay MPL and their source is available;
 * GPL-3.0 §6 instead demands complete corresponding source for the whole work,
 * which nobody can supply for JieLi's archives. This file is part of the app, so
 * it carries the app's licence. See LICENSING-NOTES.md.
 *
 * If DriftSeq ever wants to ship a BLE binary *without* publishing its own
 * source, this file and every other GPL-3.0-only one in the app must be
 * relicensed first. That is a product decision, not a build detail. */
#pragma once

/* memset too, and deliberately a declaration only: this header is
 * force-included (-include, fm1.toml o2_include) ahead of everything, so its
 * #pragma once would silence the host's real <string.h> and eas_wtsynth.c's
 * memset call would lose its declaration there. On the device the definition
 * is bsp/chip/libc.c's; on the host/web it is libc's -- the same symbol either
 * way, only the declaration lives here. memcpy and memcmp as well -- plat_host.c
 * and pack.c compile in units that include this header (the -I puts eas_inc on
 * the app's include path), so their calls would lose their declarations to
 * #pragma once the same way. */
void *memset(void *s, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
int memcmp(const void *a, const void *b, unsigned long n);

static inline int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
