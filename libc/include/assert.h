#pragma once

void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func);

#define assert(expr) \
    ((void)((expr) || (__assert_fail(#expr, __FILE__, __LINE__, __func__), 0)))