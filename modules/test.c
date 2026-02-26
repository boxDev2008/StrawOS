#include <libc.h>

static void print(const char *s)
{
    write(1, s, strlen(s));
}

const char *hello = "Hello, world!\n";

int main(void)
{
    print(hello);
    return 0;
}
