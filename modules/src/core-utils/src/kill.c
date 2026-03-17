#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "kill: usage: kill <pid>\n");
        return 1;
    }
    int pid = atoi(argv[1]);
    if (kill(pid) < 0)
    {
        fprintf(stderr, "kill: failed to kill pid %d\n", pid);
        return 1;
    }
    return 0;
}