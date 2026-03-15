#pragma once

#include <stdint.h>

enum stat_type
{
    STAT_FILE = 1,
    STAT_DIR  = 2,
    STAT_SYMLINK = 3
};

struct stat
{
    uint64_t    st_ino;
    enum stat_type st_type;
    uint64_t    st_size;
};

struct dirent
{
    enum stat_type d_type;
    char        d_name[256];
};