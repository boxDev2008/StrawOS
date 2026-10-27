#define SYS_MAX 64

typedef struct PACKED
{
    uint64_t num, a1, a2, a3, a4;
}
SyscallRegs;

typedef uint64_t (*SyscallFn)(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

static SyscallFn syscall_table[SYS_MAX] = {
    [SYS_EXIT]      = (SyscallFn)task_exit,

    [SYS_OPEN]      = (SyscallFn)fs_sys_open,
    [SYS_CLOSE]     = (SyscallFn)fs_sys_close,
    [SYS_READ]      = (SyscallFn)fs_sys_read,
    [SYS_WRITE]     = (SyscallFn)fs_sys_write,
    [SYS_IOCTL]     = (SyscallFn)fs_sys_ioctl,
    [SYS_MKDIR]     = (SyscallFn)fs_sys_mkdir,
    [SYS_LSEEK]     = (SyscallFn)fs_sys_lseek,
    [SYS_READDIR]   = (SyscallFn)fs_sys_readdir,
    [SYS_STAT]      = (SyscallFn)fs_sys_stat,
    [SYS_FSTAT]     = (SyscallFn)fs_sys_fstat,
    [SYS_UNLINK]    = (SyscallFn)fs_sys_unlink,
    
    [SYS_MEM_ALLOC] = (SyscallFn)sys_mem_alloc,
    [SYS_MEM_FREE]  = (SyscallFn)sys_mem_free,
    [SYS_CHDIR]     = (SyscallFn)sys_chdir,
    [SYS_GETCWD]    = (SyscallFn)sys_getcwd,

    [SYS_WAIT]      = (SyscallFn)task_sleep_ms,

    [SYS_SPAWN]     = (SyscallFn)sys_spawn,
    [SYS_KILL]      = (SyscallFn)sys_kill,
    [SYS_WAITPID]   = (SyscallFn)sys_task_status,
    [SYS_GETPID]    = (SyscallFn)sys_getpid,

    [SYS_SOCKET]    = (SyscallFn)sock_sys_socket,
    [SYS_BIND]      = (SyscallFn)sock_sys_bind,
    [SYS_LISTEN]    = (SyscallFn)sock_sys_listen,
    [SYS_ACCEPT]    = (SyscallFn)sock_sys_accept,
    [SYS_CONNECT]   = (SyscallFn)sock_sys_connect,
    [SYS_POLL]      = (SyscallFn)fs_sys_poll,

    [SYS_UPTIME]    = (SyscallFn)task_uptime_ms,
    [SYS_YIELD]     = (SyscallFn)task_yield,
    [SYS_RENAME]    = (SyscallFn)fs_sys_rename,
    [SYS_DUP2]      = (SyscallFn)fs_sys_dup2
};

uint64_t syscall_dispatch(SyscallRegs *r)
{
    uint64_t flags = irq_save();
    bool took_lock = !bkl_held();
    if (took_lock)
        bkl_lock();
    irq_restore(flags);

    uint64_t ret;
    if (r->num >= SYS_MAX || syscall_table[r->num] == NULL)
    {
        kprintf("Syscall: bad number %d\n", (int)r->num);
        ret = (uint64_t)-1;
    }
    else
    {
        ret = syscall_table[r->num](r->a1, r->a2, r->a3, r->a4, 0);
    }

    if (took_lock)
    {
        cli();
        task_check_kill();
        bkl_unlock();
    }
    return ret;
}

__attribute__((naked, used)) void syscall_stub(void)
{
    __asm__ volatile(
        "push %r11\n"
        "push %r10\n"
        "push %r9\n"
        "push %r8\n"
        "push %rcx\n"
        "push %rdx\n"
        "push %rsi\n"
        "push %rdi\n"
        "push %rax\n"
        "mov %rsp, %rdi\n"
        "call syscall_dispatch\n"
        "add $8, %rsp\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %r8\n"
        "pop %r9\n"
        "pop %r10\n"
        "pop %r11\n"
        "iretq\n"
    );
}