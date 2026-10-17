#pragma once
#include <features.h>

// glibc 2.43 introduced new default atan2f/sqrtf/remainderf/acosf symbol versions when removing
// legacy SVID error handling. Use the established x86-64 ABI supplied by both
// older and current libm. This changes symbol selection, not our math routines.
// https://sourceware.org/binutils/docs/as/Symver.html
// https://sourceware.org/pipermail/libc-alpha/2025-October/170912.html
#if defined(__GLIBC__) && defined(__x86_64__)
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
__asm__(".symver remainderf,remainderf@GLIBC_2.2.5");
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
#endif
