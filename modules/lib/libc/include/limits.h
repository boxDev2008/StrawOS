#pragma once

#define CHAR_BIT        8

#define PATH_MAX        256
#define NAME_MAX_POSIX  255
#define IOV_MAX         64
#define SCHAR_MIN       (-128)
#define SCHAR_MAX       127
#define UCHAR_MAX       255

#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN        0
#define CHAR_MAX        UCHAR_MAX
#else
#define CHAR_MIN        SCHAR_MIN
#define CHAR_MAX        SCHAR_MAX
#endif

#define MB_LEN_MAX      2

#define SHRT_MIN        (-32768)
#define SHRT_MAX        32767
#define USHRT_MAX       65535

#ifndef INT_MIN
#define INT_MIN (-2147483647 - 1)
#endif
#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

#define LONG_MIN        (-LONG_MAX - 1L)
#ifdef __LP64__
#define LONG_MAX        9223372036854775807L
#define ULONG_MAX       18446744073709551615UL
#else
#define LONG_MAX        2147483647L
#define ULONG_MAX       4294967295UL
#endif
#define LLONG_MAX       9223372036854775807LL
#define LLONG_MIN       (-LLONG_MAX - 1LL)
#define ULLONG_MAX      18446744073709551615ULL
