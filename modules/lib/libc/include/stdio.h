#pragma once

#include <stddef.h>
#include <stdarg.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define BUFSIZ 512

#define FILE_BUF_SIZE  4096

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define L_tmpnam 32
#define TMP_MAX  10000
#define EOF            (-1)
 
typedef struct FILE
{
    int fd;
    int error;
    int eof;
    int bufmode;
 
    char rbuf[FILE_BUF_SIZE];
    int rpos;
    int rlen;
 
    char wbuf[FILE_BUF_SIZE];
    int wlen;
}
FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
int fclose(FILE *f);
int fflush(FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
FILE *tmpfile(void);
char *tmpnam(char *s);
size_t fread(void *buf, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *f);

int fgetc(FILE *f);
#define getc(f) fgetc(f)

int fputc(int c, FILE *f);
#define putc(c, f) fputc(c, f)

int ungetc(int c, FILE *f);
int putchar(int c);

int mkdir(const char *pathname, unsigned int mode);
int remove(const char *path);
int rename(const char *from, const char *to);

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