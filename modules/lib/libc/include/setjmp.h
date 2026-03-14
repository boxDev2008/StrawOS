#pragma once

typedef struct
{
    void *rsp;
    void *rbp;
    void *rip;
    void *rbx;
    void *r12;
    void *r13;
    void *r14;
    void *r15;
}
jmp_buf[1];

int setjmp(jmp_buf env);
int _setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);