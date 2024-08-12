#ifndef _STDIO_H
#define _STDIO_H

#define __need_va_list
#define __need_size_t
#define __need_NULL

#include <stdarg.h>
#include <stddef.h>

#define _IOFBF          0
#define _IOLBF          1
#define _IONBF          2

#define BUFSIZ          1024
#define EOF             (-1)
#define FOPEN_MAX       20
#define FILENAME_MAX    260
#define L_tmpnam        255

#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

#define TMP_MAX         32767

#include <sys/cdefs.h>

__BEGIN_DECLS

typedef struct {
    int _file;
    int _flag;
    int _charbuf;
    char *_base;
    char *_end;
    char *_ptr;
    char *_rend;
    char *_wend;
    char *_tmpfname;
} FILE;

__extension__ typedef __int64 fpos_t;

void printf(const char *, ...);

int sprintf(char *, const char *, ...);

int snprintf(char *, size_t, const char *, ...);

int vfprintf(FILE *, const char *, va_list);

int vprintf(const char *, va_list);

int vsnprintf(char* , size_t, const char *, va_list);

int vsprintf(char *, const char *, va_list);

__END_DECLS

#endif /* _STDIO_H */
