#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "mkdir: usage: mkdir <dir> [dir...]\n");
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++)
    {
        if (mkdir(argv[i], 0) < 0)
        {
            fprintf(stderr, "mkdir: cannot create '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}