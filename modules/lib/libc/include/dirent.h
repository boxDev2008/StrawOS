#pragma once

#include <sys/stat.h>

#define NAME_MAX 64

struct dirent
{
    char name[NAME_MAX];
    enum stat_type type;
};

int readdir(int fd, struct dirent *out);
