#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int newline = 1;
    int start   = 1;

    if (argc >= 2 && strcmp(argv[1], "-n") == 0)
    {
        newline = 0;
        start   = 2;
    }

    for (int i = start; i < argc; i++)
    {
        if (i > start) putchar(' ');
        fputs(argv[i], stdout);
    }
    if (newline) putchar('\n');
    fflush(stdout);
    return 0;
}