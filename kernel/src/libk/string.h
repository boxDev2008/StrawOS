#pragma once

#include <stddef.h>

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strcat(char *dst, const char *src);