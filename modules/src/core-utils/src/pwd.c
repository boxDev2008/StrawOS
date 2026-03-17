#include <stdio.h>
#include <unistd.h>

#define CWD_MAX 512

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char buf[CWD_MAX];
    if (!getcwd(buf, sizeof(buf)))
    {
        fprintf(stderr, "pwd: failed\n");
        return 1;
    }
    puts(buf);
    return 0;
}