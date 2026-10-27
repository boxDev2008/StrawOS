#include <stdio.h>
#include <unistd.h>

int main(void)
{
    if (!spawn("/bin/strawx.elf", NULL))
    {
        printf("init: failed to start the display server\n");
        return 1;
    }
    return 0;
}
