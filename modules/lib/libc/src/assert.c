#include <assert.h>

void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func)
{
    (void)expr; (void)file; (void)line; (void)func;
    while (1) {}
}