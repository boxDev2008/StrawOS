#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "rm: usage: rm <file> [file...]\n");
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++)
    {
        if (remove(argv[i]) < 0)
        {
            fprintf(stderr, "rm: cannot remove '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}