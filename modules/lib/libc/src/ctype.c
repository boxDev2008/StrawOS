#include <ctype.h>

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
#define SPACE_  (_ISspace | _IScntrl)           /* \t \n \v \f \r        */
#define SP      (_ISspace | _ISprint | _ISblank) /* 0x20 SPACE            */
#define CTRL    (_IScntrl)
#define NUL     (_IScntrl)

/* 256-entry body of the table (index 128 … 383). */
#define CTYPE_TABLE_BODY \
/* 0x00 NUL */ NUL,      \
/* 0x01     */ CTRL,     \
/* 0x02     */ CTRL,     \
/* 0x03     */ CTRL,     \
/* 0x04     */ CTRL,     \
/* 0x05     */ CTRL,     \
/* 0x06     */ CTRL,     \
/* 0x07 BEL */ CTRL,     \
/* 0x08 BS  */ CTRL,     \
/* 0x09 HT  */ (_IScntrl|_ISspace|_ISblank), \
/* 0x0A LF  */ (_IScntrl|_ISspace),          \
/* 0x0B VT  */ (_IScntrl|_ISspace),          \
/* 0x0C FF  */ (_IScntrl|_ISspace),          \
/* 0x0D CR  */ (_IScntrl|_ISspace),          \
/* 0x0E     */ CTRL,     \
/* 0x0F     */ CTRL,     \
/* 0x10     */ CTRL,     \
/* 0x11     */ CTRL,     \
/* 0x12     */ CTRL,     \
/* 0x13     */ CTRL,     \
/* 0x14     */ CTRL,     \
/* 0x15     */ CTRL,     \
/* 0x16     */ CTRL,     \
/* 0x17     */ CTRL,     \
/* 0x18     */ CTRL,     \
/* 0x19     */ CTRL,     \
/* 0x1A     */ CTRL,     \
/* 0x1B ESC */ CTRL,     \
/* 0x1C     */ CTRL,     \
/* 0x1D     */ CTRL,     \
/* 0x1E     */ CTRL,     \
/* 0x1F     */ CTRL,     \
/* 0x20 SP  */ SP,       \
/* 0x21  !  */ PUNCT,    \
/* 0x22  "  */ PUNCT,    \
/* 0x23  #  */ PUNCT,    \
/* 0x24  $  */ PUNCT,    \
/* 0x25  %  */ PUNCT,    \
/* 0x26  &  */ PUNCT,    \
/* 0x27  '  */ PUNCT,    \
/* 0x28  (  */ PUNCT,    \
/* 0x29  )  */ PUNCT,    \
/* 0x2A  *  */ PUNCT,    \
/* 0x2B  +  */ PUNCT,    \
/* 0x2C  ,  */ PUNCT,    \
/* 0x2D  -  */ PUNCT,    \
/* 0x2E  .  */ PUNCT,    \
/* 0x2F  /  */ PUNCT,    \
/* 0x30  0  */ DIGIT,    \
/* 0x31  1  */ DIGIT,    \
/* 0x32  2  */ DIGIT,    \
/* 0x33  3  */ DIGIT,    \
/* 0x34  4  */ DIGIT,    \
/* 0x35  5  */ DIGIT,    \
/* 0x36  6  */ DIGIT,    \
/* 0x37  7  */ DIGIT,    \
/* 0x38  8  */ DIGIT,    \
/* 0x39  9  */ DIGIT,    \
/* 0x3A  :  */ PUNCT,    \
/* 0x3B  ;  */ PUNCT,    \
/* 0x3C  <  */ PUNCT,    \
/* 0x3D  =  */ PUNCT,    \
/* 0x3E  >  */ PUNCT,    \
/* 0x3F  ?  */ PUNCT,    \
/* 0x40  @  */ PUNCT,    \
/* 0x41  A  */ HEX_UL,   \
/* 0x42  B  */ HEX_UL,   \
/* 0x43  C  */ HEX_UL,   \
/* 0x44  D  */ HEX_UL,   \
/* 0x45  E  */ HEX_UL,   \
/* 0x46  F  */ HEX_UL,   \
/* 0x47  G  */ UPPER,    \
/* 0x48  H  */ UPPER,    \
/* 0x49  I  */ UPPER,    \
/* 0x4A  J  */ UPPER,    \
/* 0x4B  K  */ UPPER,    \
/* 0x4C  L  */ UPPER,    \
/* 0x4D  M  */ UPPER,    \
/* 0x4E  N  */ UPPER,    \
/* 0x4F  O  */ UPPER,    \
/* 0x50  P  */ UPPER,    \
/* 0x51  Q  */ UPPER,    \
/* 0x52  R  */ UPPER,    \
/* 0x53  S  */ UPPER,    \
/* 0x54  T  */ UPPER,    \
/* 0x55  U  */ UPPER,    \
/* 0x56  V  */ UPPER,    \
/* 0x57  W  */ UPPER,    \
/* 0x58  X  */ UPPER,    \
/* 0x59  Y  */ UPPER,    \
/* 0x5A  Z  */ UPPER,    \
/* 0x5B  [  */ PUNCT,    \
/* 0x5C  \  */ PUNCT,    \
/* 0x5D  ]  */ PUNCT,    \
/* 0x5E  ^  */ PUNCT,    \
/* 0x5F  _  */ PUNCT,    \
/* 0x60  `  */ PUNCT,    \
/* 0x61  a  */ HEX_LL,   \
/* 0x62  b  */ HEX_LL,   \
/* 0x63  c  */ HEX_LL,   \
/* 0x64  d  */ HEX_LL,   \
/* 0x65  e  */ HEX_LL,   \
/* 0x66  f  */ HEX_LL,   \
/* 0x67  g  */ LOWER,    \
/* 0x68  h  */ LOWER,    \
/* 0x69  i  */ LOWER,    \
/* 0x6A  j  */ LOWER,    \
/* 0x6B  k  */ LOWER,    \
/* 0x6C  l  */ LOWER,    \
/* 0x6D  m  */ LOWER,    \
/* 0x6E  n  */ LOWER,    \
/* 0x6F  o  */ LOWER,    \
/* 0x70  p  */ LOWER,    \
/* 0x71  q  */ LOWER,    \
/* 0x72  r  */ LOWER,    \
/* 0x73  s  */ LOWER,    \
/* 0x74  t  */ LOWER,    \
/* 0x75  u  */ LOWER,    \
/* 0x76  v  */ LOWER,    \
/* 0x77  w  */ LOWER,    \
/* 0x78  x  */ LOWER,    \
/* 0x79  y  */ LOWER,    \
/* 0x7A  z  */ LOWER,    \
/* 0x7B  {  */ PUNCT,    \
/* 0x7C  |  */ PUNCT,    \
/* 0x7D  }  */ PUNCT,    \
/* 0x7E  ~  */ PUNCT,    \
/* 0x7F DEL */ CTRL,     \
/* 0x80-0xFF: high bytes — not classified in "C" locale */ \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

/* Full 384-entry table: 128 guard zeros + 256 body entries. */
const uint16_t __ctype_b[384] = {
    /* guard: indices 0-127 (represents values -128 to -1, all zero) */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* body: indices 128-383 (unsigned char 0x00-0xFF) */
    CTYPE_TABLE_BODY
};

/* -------------------------------------------------------------------------
 * Case-conversion tables
 *
 * Identity for everything outside A-Z / a-z.
 * Indexed the same way: table[128 + c].
 * -------------------------------------------------------------------------
 *
 * We build the 256-entry body as a macro that emits one value per byte.
 * For brevity the control/high range entries are written as range comments.
 */

#define TL_BODY \
/* 0x00-0x40: identity */ \
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, \
 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, \
 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, \
 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, \
 64, \
/* 0x41-0x5A A-Z → a-z */ \
 97, 98, 99,100,101,102,103,104,105,106,107,108,109, \
110,111,112,113,114,115,116,117,118,119,120,121,122, \
/* 0x5B-0x60: identity */ \
 91, 92, 93, 94, 95, 96, \
/* 0x61-0x7A a-z: already lower, identity */ \
 97, 98, 99,100,101,102,103,104,105,106,107,108,109, \
110,111,112,113,114,115,116,117,118,119,120,121,122, \
/* 0x7B-0xFF: identity */ \
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
/* 0x00-0x40: identity */ \
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, \
 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, \
 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, \
 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, \
 64, \
/* 0x41-0x5A A-Z: already upper, identity */ \
 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, \
 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, \
/* 0x5B-0x60: identity */ \
 91, 92, 93, 94, 95, 96, \
/* 0x61-0x7A a-z → A-Z */ \
 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, \
 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, \
/* 0x7B-0xFF: identity */ \
123,124,125,126,127, \
128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143, \
144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159, \
160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175, \
176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191, \
192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207, \
208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223, \
224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239, \
240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255

/* 128 guard entries (identity values -128 to -1) + 256 body entries. */
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

/* -------------------------------------------------------------------------
 * _loc() functions
 *
 * Each returns a pointer to a static pointer that is pre-aimed at
 * index 128 of the corresponding table — i.e. at the entry for c == 0.
 * The caller then does ptr[c] which resolves to table[128 + c].
 * ------------------------------------------------------------------------- */

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