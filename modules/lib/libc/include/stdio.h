#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <syscall.h>

#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_ACCMODE   0x003
#define O_CREAT     0x040
#define O_TRUNC     0x200
#define O_APPEND    0x400
#define O_DIRECTORY 0x10000

#define S_TYPE_FILE 1
#define S_TYPE_DIR 2
#define S_TYPE_SYMLINK 3

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define BUFSIZ 512

typedef enum
{
    STAT_FILE = 1,
    STAT_DIR  = 2
}
stat_type_t;

typedef struct stat
{
    uint64_t    st_ino;
    stat_type_t st_type;
    uint64_t    st_size;
}
stat_t;

typedef struct dirent
{
    stat_type_t d_type;
    char        d_name[256];
}
dirent_t;

#define FILE_BUF_SIZE  4096
#define EOF            (-1)
 
typedef struct FILE
{
    int     fd;             /* underlying file descriptor              */
    int     error;          /* sticky error flag                       */
    int     eof;            /* sticky EOF flag                         */
 
    /* read buffer */
    char    rbuf[FILE_BUF_SIZE];
    int     rpos;           /* next byte to consume                    */
    int     rlen;           /* valid bytes in rbuf                     */
 
    /* write buffer */
    char    wbuf[FILE_BUF_SIZE];
    int     wlen;           /* bytes pending in wbuf                   */
}
FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/**
 * fopen - open a file and return a FILE *.
 *
 * mode strings supported:
 *   "r"   O_RDONLY
 *   "w"   O_WRONLY | O_CREAT | O_TRUNC
 *   "a"   O_WRONLY | O_CREAT | O_APPEND
 *   "r+"  O_RDWR
 *   "w+"  O_RDWR   | O_CREAT | O_TRUNC
 *   "a+"  O_RDWR   | O_CREAT | O_APPEND
 *
 * Returns NULL on error.
 */
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
int fclose(FILE *f);
int fflush(FILE *f);
size_t fread(void *buf, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *f);

int fgetc(FILE *f);
#define getc(f) fgetc(f)

int fputc(int c, FILE *f);
#define putc(c, f) fputc(c, f)

int ungetc(int c, FILE *f);

char *fgets(char *s, int n, FILE *f);
int fputs(const char *s, FILE *f);
int puts(const char *s);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int  feof(FILE *f);
int  ferror(FILE *f);
void clearerr(FILE *f);

int vfprintf(FILE *f, const char *fmt, va_list ap);
int fprintf(FILE *f, const char *fmt, ...);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int printf(const char *fmt, ...);