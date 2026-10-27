#pragma once

#include <stddef.h>

typedef void *iconv_t;

iconv_t iconv_open(const char *to, const char *from);
size_t  iconv(iconv_t cd, char **in, size_t *in_left, char **out, size_t *out_left);
int     iconv_close(iconv_t cd);
