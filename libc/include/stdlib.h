#pragma once

#include <stddef.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2

void  *segbrk(void *addr);
void  *mmap(size_t len, int prot);
int    munmap(void *addr, size_t len);

void  *malloc(size_t size);
void   free(void *ptr);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);

int    exit(int code);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));