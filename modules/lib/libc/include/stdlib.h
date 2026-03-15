#pragma once

#include <stddef.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

int abs(int x);

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
int system(const char *command);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

unsigned long long strtoull(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
long long strtoll(const char *s, char **endptr, int base);
long strtol(const char *s, char **endptr, int base);
int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
double strtod(const char *s, char **endptr);
float strtof(const char *s, char **endptr);
double atof(const char *s);
char *itoa(int value, char *buf, int base);