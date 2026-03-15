#pragma once

#include <stddef.h>

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strcat(char *dst, const char *src);
char *strstr(const char *s, const char *needle);
char *strrstr(const char *s, const char *needle);
char *strrchr(const char *s, int c);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strtok_r(char *s, const char *delim, char **saveptr);
char *strtok(char *s, const char *delim);

int strcoll(const char *a, const char *b);
size_t strxfrm(char *dst, const char *src, size_t n);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
char *strncat(char *dst, const char *src, size_t n);
char *strerror(int errnum);
int strerror_r(int errnum, char *buf, size_t buflen);