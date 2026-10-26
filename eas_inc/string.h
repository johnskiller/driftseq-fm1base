/* SPDX-License-Identifier: GPL-3.0-only */
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
 * WHY GPL HERE IS FINE, when it was not in the other tree. This file is
 * DriftSeq's own code (GPL-3.0-only, like the rest of DriftSeq) and it is
 * compiled into DriftSeq's own image. The licensing problem the EAS tree had was
 * different in kind: there the GPL header sat on the include path of a firmware
 * whose whole point was to link JieLi's Apache-2.0 *binary* Bluetooth libraries,
 * which GPL-3.0 cannot accompany with complete source. Here the binary question
 * is answered by fm1-base being MPL-2.0 instead — MPL combines with Apache-2.0
 * binaries freely — and DriftSeq's own GPL files do not change that, because
 * DriftSeq publishes its own complete source.
 *
 * If DriftSeq ever wants to ship a BLE binary *without* publishing its own
 * source, this file and every other GPL-3.0-only one in the app must be
 * relicensed first. That is a product decision, not a build detail. */
#pragma once

static inline int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
