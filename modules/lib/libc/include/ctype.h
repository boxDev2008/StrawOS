#pragma once

#include <stdint.h>

#define _ISupper   (1u << 8)   /* A-Z          */
#define _ISlower   (1u << 9)   /* a-z          */
#define _ISalpha   (1u << 10)  /* isalpha      */
#define _ISdigit   (1u << 11)  /* 0-9          */
#define _ISxdigit  (1u << 12)  /* 0-9 A-F a-f  */
#define _ISspace   (1u << 13)  /* whitespace   */
#define _ISprint   (1u << 14)  /* printable    */
#define _ISgraph   (1u << 15)  /* visible      */
#define _ISblank   (1u << 0)   /* SP HT        */
#define _IScntrl   (1u << 1)   /* control      */
#define _ISpunct   (1u << 2)   /* punctuation  */
#define _ISalnum   (1u << 3)   /* isalnum      */

extern const uint16_t __ctype_b[384];
extern const int32_t  __ctype_tolower[384];
extern const int32_t  __ctype_toupper[384];

const uint16_t **__ctype_b_loc(void);
const int32_t  **__ctype_tolower_loc(void);
const int32_t  **__ctype_toupper_loc(void);

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);

int tolower(int c);
int toupper(int c);

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

#define isalnum(c)  ((*__ctype_b_loc())[(c)] & _ISalnum)
#define isalpha(c)  ((*__ctype_b_loc())[(c)] & _ISalpha)
#define isblank(c)  ((*__ctype_b_loc())[(c)] & _ISblank)
#define iscntrl(c)  ((*__ctype_b_loc())[(c)] & _IScntrl)
#define isdigit(c)  ((*__ctype_b_loc())[(c)] & _ISdigit)
#define isgraph(c)  ((*__ctype_b_loc())[(c)] & _ISgraph)
#define islower(c)  ((*__ctype_b_loc())[(c)] & _ISlower)
#define isprint(c)  ((*__ctype_b_loc())[(c)] & _ISprint)
#define ispunct(c)  ((*__ctype_b_loc())[(c)] & _ISpunct)
#define isspace(c)  ((*__ctype_b_loc())[(c)] & _ISspace)
#define isupper(c)  ((*__ctype_b_loc())[(c)] & _ISupper)
#define isxdigit(c) ((*__ctype_b_loc())[(c)] & _ISxdigit)

#define tolower(c)  ((*__ctype_tolower_loc())[(c)])
#define toupper(c)  ((*__ctype_toupper_loc())[(c)])