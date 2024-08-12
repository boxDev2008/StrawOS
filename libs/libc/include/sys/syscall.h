#pragma once

#include <stddef.h>

void syscall_yield(void);
void syscall_print(const char *data, size_t size);
void *syscall_malloc(size_t size);
void *syscall_calloc(size_t nitems, size_t size);
void *syscall_realloc(void *ptr, size_t size);
void syscall_free(void *ptr);