#pragma once

#include <sys/types.h>

#define dirent    ns_dirent
#define DIR       ns_DIR
#define opendir   ns_opendir
#define fdopendir ns_fdopendir
#define readdir   ns_readdir
#define closedir  ns_closedir
#define rewinddir ns_rewinddir
#define dirfd     ns_dirfd
#define scandir   ns_scandir
#define alphasort ns_alphasort

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12

struct ns_dirent
{
    ino_t         d_ino;
    off_t         d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char          d_name[256];
};

typedef struct ns_DIR ns_DIR;

ns_DIR           *ns_opendir(const char *path);
ns_DIR           *ns_fdopendir(int fd);
struct ns_dirent *ns_readdir(ns_DIR *dir);
int               ns_closedir(ns_DIR *dir);
void              ns_rewinddir(ns_DIR *dir);
int               ns_dirfd(ns_DIR *dir);
int               ns_scandir(const char *path, struct ns_dirent ***list,
                             int (*filter)(const struct ns_dirent *),
                             int (*compar)(const struct ns_dirent **, const struct ns_dirent **));
int               ns_alphasort(const struct ns_dirent **a, const struct ns_dirent **b);
