#pragma once

#include <stdint.h>

/* d_type values — mirror the kernel's VDirent types */
#define DT_UNKNOWN  0
#define DT_FILE     1   /* regular file  (DIRENT_FILE) */
#define DT_DIR      2   /* directory     (DIRENT_DIR)  */

/* Legacy enum kept for source compatibility */
enum dirent_type
{
    DIRENT_FILE = 1,
    DIRENT_DIR  = 2,
};

struct dirent
{
    enum dirent_type d_type;
    char             d_name[256];
};

typedef struct DIR
{
    int      fd;
    uint64_t index;     /* next entry index for readdir() */
} DIR;

/* ── Standard POSIX-style API ──────────────────────────────────────────── */

/** Open a directory stream for the directory at @name.
 *  Returns NULL on error. */
DIR            *opendir(const char *name);

/** Rewind directory stream to the beginning. */
void            rewinddir(DIR *dir);

/** Read the next directory entry.
 *  Returns a pointer to a statically-allocated dirent, or NULL at end /
 *  error. */
struct dirent  *readdir(DIR *dir);

/** Low-level indexed read (original API, still available).
 *  Fills @out and returns 0 on success, negative on error / end. */
int             readdir_r(DIR *dir, uint64_t index, struct dirent *out);

/** Close a directory stream opened with opendir(). */
int             closedir(DIR *dir);