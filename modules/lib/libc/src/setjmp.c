__attribute__((naked)) int setjmp(jmp_buf env)
{
    __asm__ volatile (
        "mov  %%rbx, 0(%%rdi)   \n\t"
        "mov  %%rbp, 8(%%rdi)   \n\t"
        "mov  %%r12, 16(%%rdi)  \n\t"
        "mov  %%r13, 24(%%rdi)  \n\t"
        "mov  %%r14, 32(%%rdi)  \n\t"
        "mov  %%r15, 40(%%rdi)  \n\t"
        "lea  8(%%rsp), %%rax   \n\t"
        "mov  %%rax, 48(%%rdi)  \n\t"
        "mov  (%%rsp), %%rax    \n\t"
        "mov  %%rax, 56(%%rdi)  \n\t"
        "xor  %%eax, %%eax      \n\t"
        "ret                    \n\t"
        :
        :
        : "memory"
    );
}

__attribute__((naked)) int _setjmp(jmp_buf env)
{
    __asm__ volatile ("jmp setjmp\n\t");
}

__attribute__((naked)) void longjmp(jmp_buf env, int val)
{
    __asm__ volatile (
        "mov  %%esi, %%eax      \n\t"
        "test %%eax, %%eax      \n\t"
        "jnz  1f                \n\t"
        "mov  $1, %%eax         \n\t"
        "1:                     \n\t"
        "mov  0(%%rdi), %%rbx   \n\t"
        "mov  8(%%rdi), %%rbp   \n\t"
        "mov  16(%%rdi), %%r12  \n\t"
        "mov  24(%%rdi), %%r13  \n\t"
        "mov  32(%%rdi), %%r14  \n\t"
        "mov  40(%%rdi), %%r15  \n\t"
        "mov  48(%%rdi), %%rsp  \n\t"
        "jmp  *56(%%rdi)        \n\t"
        :
        :
        : "memory"
    );
}

__attribute__((naked)) void _longjmp(jmp_buf env, int val)
{
    __asm__ volatile ("jmp longjmp\n\t");
}