#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
    return 0;
}