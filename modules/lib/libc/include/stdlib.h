#pragma once

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

int abs(int x);

void  *malloc(size_t size);
void   free(void *ptr);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);

__attribute__((noreturn)) void abort(void);
__attribute__((noreturn)) void exit(int code);

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
long double strtold(const char *s, char **endptr);
double atof(const char *s);
char *itoa(int value, char *buf, int base);

#define RAND_MAX 2147483647

int   rand(void);
void  srand(unsigned int seed);
int   rand_r(unsigned int *seed);
long  random(void);
void  srandom(unsigned int seed);
int   atexit(void (*fn)(void));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
long  labs(long x);
long long llabs(long long x);
int   mkstemp(char *tmpl);
int   setenv(const char *name, const char *value, int overwrite);
int   unsetenv(const char *name);

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
div_t  div(int a, int b);
ldiv_t ldiv(long a, long b);