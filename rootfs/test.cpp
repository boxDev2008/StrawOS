#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <sys/syscall.h>

int main(void)
{
    char *name = (char*)malloc(8);
    memset(name, 0, 8);
    name[0] = 'a';
    free(name);
    char *name2 = (char*)malloc(8);
    printf("Hello World! :3, %s\n", name2);
    free(name);
    syscall_yield();
    return 0;
}