#pragma once

#include <stddef.h>

typedef long regoff_t;

typedef struct
{
    size_t re_nsub;
}
regex_t;

typedef struct
{
    regoff_t rm_so;
    regoff_t rm_eo;
}
regmatch_t;

#define REG_EXTENDED 1
#define REG_ICASE    2
#define REG_NOSUB    4
#define REG_NEWLINE  8

#define REG_NOMATCH 1
#define REG_ESPACE  12

int    regcomp(regex_t *re, const char *pattern, int flags);
int    regexec(const regex_t *re, const char *s, size_t nmatch, regmatch_t *match, int flags);
size_t regerror(int code, const regex_t *re, char *buf, size_t size);
void   regfree(regex_t *re);
