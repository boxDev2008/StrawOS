#include <setjmp.h>

int setjmp(jmp_buf env)
{
    return __builtin_setjmp(env);
}

int _setjmp(jmp_buf env)
{
    return __builtin_setjmp(env);
}

void longjmp(jmp_buf env, int val)
{
    __builtin_longjmp(env, 1);
}