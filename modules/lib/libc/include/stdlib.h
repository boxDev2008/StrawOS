#pragma once

#include <stddef.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void  *mmap(size_t len, int prot);
int    munmap(void *addr, size_t len);

void  *malloc(size_t size);
void   free(void *ptr);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);

void   abort(void);
int    exit(int code);
void   yield(void);

char *getenv(const char *name);

int abs(int x);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));