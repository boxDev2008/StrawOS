#include <stdio.h>
#include <unistd.h>

#define COL_RESET "\033[0m"
#define COL_BOLD  "\033[1m"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf(COL_BOLD "  PID  COMMAND" COL_RESET "\n");
    printf("  %d  ps\n", getpid());
    return 0;
}