#include <locale.h>

#define CHAR_MAX 127

static char _empty[] = "";
static char _dot[]   = ".";
static char _C[]     = "C";

static struct lconv _c_locale = {
    /* Numeric */
    .decimal_point      = _dot,
    .thousands_sep      = _empty,
    .grouping           = _empty,

    /* Monetary */
    .int_curr_symbol    = _empty,
    .currency_symbol    = _empty,
    .mon_decimal_point  = _empty,
    .mon_thousands_sep  = _empty,
    .mon_grouping       = _empty,
    .positive_sign      = _empty,
    .negative_sign      = _empty,
    .int_frac_digits    = CHAR_MAX,
    .frac_digits        = CHAR_MAX,
    .p_cs_precedes      = CHAR_MAX,
    .p_sep_by_space     = CHAR_MAX,
    .n_cs_precedes      = CHAR_MAX,
    .n_sep_by_space     = CHAR_MAX,
    .p_sign_posn        = CHAR_MAX,
    .n_sign_posn        = CHAR_MAX,
};

static int streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

char *setlocale(int category, const char *locale)
{
    (void)category;

    if (locale == NULL)
        return _C;

    if (locale[0] == '\0' || streq(locale, "C")|| streq(locale, "POSIX"))
        return _C;

    return NULL;
}

struct lconv *localeconv(void)
{
    return &_c_locale;
}