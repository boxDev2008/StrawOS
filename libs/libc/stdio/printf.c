#include "stdio.h"
#include "string.h"
#include "libprintf.h"
#include "sys/syscall.h"

const char printf_buffer[5120];

void printf(const char *fmt, ...)
{
    va_list ptr;
    va_start(ptr, fmt);
    vsnprintf((char *)&printf_buffer, -1, fmt, ptr);

    syscall_print((char*)printf_buffer, strlen(printf_buffer));

    va_end(ptr);
}