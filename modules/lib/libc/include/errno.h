#pragma once

#define ENOENT    2
#define EBADF     9
#define ENOMEM    12
#define EACCES    13
#define EEXIST    17
#define ENOTDIR   20
#define EISDIR    21
#define EINVAL    22
#define EMFILE    24
#define ENOSPC    28
#define ENOTEMPTY 39

extern int errno;
int *__errno_location(void);