#pragma once

#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)

#ifdef __cplusplus
#define NULL (void*)0
#else
#define NULL 0
#endif

typedef long long intptr_t;
typedef unsigned long long uintptr_t;

typedef long long ptrdiff_t;

typedef unsigned long long size_t;