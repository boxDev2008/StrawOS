#pragma once

#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

#ifndef __INTPTR_DEFINED
#define __INTPTR_DEFINED
typedef long long intptr_t;
typedef unsigned long long uintptr_t;
#endif

typedef long long ptrdiff_t;

typedef unsigned long long size_t;