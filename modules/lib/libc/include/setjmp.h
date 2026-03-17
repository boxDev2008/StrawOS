#pragma once

#include <stdint.h>

typedef uint64_t jmp_buf[8];
 
int  setjmp(jmp_buf env);
int  _setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
void _longjmp(jmp_buf env, int val);