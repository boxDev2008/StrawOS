#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <syscall.h>

#define BUFSIZ 512

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

int mkdir(const char *pathname, unsigned int mode);
int remove(const char *path);
int rename(const char *from, const char *to);

int putchar(int c);

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

int sscanf(const char *buf, const char *fmt, ...);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int printf(const char *fmt, ...);