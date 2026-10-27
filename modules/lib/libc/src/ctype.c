#define U   _ISupper
#define L   _ISlower
#define D   _ISdigit
#define S   _ISspace
#define P   _ISpunct
#define X   _ISxdigit
#define BL  _ISblank
#define CN  _IScntrl

#define ALPHA   (_ISalpha | _ISprint | _ISgraph)
#define DIGIT   (_ISdigit | _ISprint | _ISgraph | _ISalnum | _ISxdigit)
#define HEX_UL  (_ISupper | _ISalpha | _ISprint | _ISgraph | _ISalnum | _ISxdigit)
#define HEX_LL  (_ISlower | _ISalpha | _ISprint | _ISgraph | _ISalnum | _ISxdigit)
#define UPPER   (_ISupper | _ISalpha | _ISprint | _ISgraph | _ISalnum)
#define LOWER   (_ISlower | _ISalpha | _ISprint | _ISgraph | _ISalnum)
#define PUNCT   (_ISpunct | _ISprint | _ISgraph)
#define SPACE_  (_ISspace | _IScntrl)
#define SP      (_ISspace | _ISprint | _ISblank)
#define CTRL    (_IScntrl)
#define NUL     (_IScntrl)

#define CTYPE_TABLE_BODY \
NUL,      \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
(_IScntrl|_ISspace|_ISblank), \
(_IScntrl|_ISspace),          \
(_IScntrl|_ISspace),          \
(_IScntrl|_ISspace),          \
(_IScntrl|_ISspace),          \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
CTRL,     \
SP,       \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
DIGIT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
HEX_UL,   \
HEX_UL,   \
HEX_UL,   \
HEX_UL,   \
HEX_UL,   \
HEX_UL,   \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
UPPER,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
HEX_LL,   \
HEX_LL,   \
HEX_LL,   \
HEX_LL,   \
HEX_LL,   \
HEX_LL,   \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
LOWER,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
PUNCT,    \
CTRL,     \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

const uint16_t __ctype_b[384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    CTYPE_TABLE_BODY
};

#define TL_BODY \
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, \
 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, \
 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, \
 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, \
 64, \
 97, 98, 99,100,101,102,103,104,105,106,107,108,109, \
110,111,112,113,114,115,116,117,118,119,120,121,122, \
 91, 92, 93, 94, 95, 96, \
 97, 98, 99,100,101,102,103,104,105,106,107,108,109, \
110,111,112,113,114,115,116,117,118,119,120,121,122, \
123,124,125,126,127, \
128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143, \
144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159, \
160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175, \
176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191, \
192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207, \
208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223, \
224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239, \
240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255

#define TU_BODY \
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, \
 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, \
 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, \
 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, \
 64, \
 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, \
 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, \
 91, 92, 93, 94, 95, 96, \
 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, \
 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, \
123,124,125,126,127, \
128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143, \
144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159, \
160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175, \
176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191, \
192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207, \
208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223, \
224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239, \
240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255

#define GUARD_INT \
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, \
 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, \
 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, \
 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, \
 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, \
 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, \
 96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111, \
112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127

const int32_t __ctype_tolower[384] = { GUARD_INT, TL_BODY };
const int32_t __ctype_toupper[384] = { GUARD_INT, TU_BODY };

const uint16_t **__ctype_b_loc(void)
{
    static const uint16_t *p = &__ctype_b[128];
    return &p;
}

const int32_t **__ctype_tolower_loc(void)
{
    static const int32_t *p = &__ctype_tolower[128];
    return &p;
}

const int32_t **__ctype_toupper_loc(void)
{
    static const int32_t *p = &__ctype_toupper[128];
    return &p;
}

#undef isalnum
#undef isalpha
#undef isblank
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef tolower
#undef toupper

int isalnum(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISalnum;  }
int isalpha(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISalpha;  }
int isblank(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISblank;  }
int iscntrl(int c)  { return __ctype_b[128 + (unsigned char)c] & _IScntrl;  }
int isdigit(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISdigit;  }
int isgraph(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISgraph;  }
int islower(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISlower;  }
int isprint(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISprint;  }
int ispunct(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISpunct;  }
int isspace(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISspace;  }
int isupper(int c)  { return __ctype_b[128 + (unsigned char)c] & _ISupper;  }
int isxdigit(int c) { return __ctype_b[128 + (unsigned char)c] & _ISxdigit; }

int tolower(int c)  { return __ctype_tolower[128 + (unsigned char)c]; }
int toupper(int c)  { return __ctype_toupper[128 + (unsigned char)c]; }