#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "cat: usage: cat <file> [file...]\n");
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++)
    {
        FILE *f = fopen(argv[i], "r");
        if (!f)
        {
            fprintf(stderr, "cat: cannot open '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            fwrite(buf, 1, n, stdout);
        fclose(f);
    }
    return rc;
}