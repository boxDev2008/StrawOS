#include <strings.h>

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        unsigned char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        unsigned char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;

        if (c1 != c2)
            return c1 - c2;

        s1++;
        s2++;
    }

    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s2)
    {
        unsigned char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        unsigned char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;

        if (c1 != c2)
            return c1 - c2;

        s1++; s2++; n--;
    }

    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}