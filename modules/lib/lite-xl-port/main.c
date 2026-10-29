#include <stdio.h>
#include <stdlib.h>
#include <strawx.h>

/* The real entry point: lite-xl's own main() is renamed by our SDL_main.h */

#define LITE_XL_EXE     "/bin/lite-xl.elf"
#define LITE_XL_USERDIR "/config/lite-xl"

int lite_xl_main(int argc, char **argv);

int main(int argc, char **argv)
{
    if (sx_connect() != 0)
    {
        printf("lite-xl: cannot connect to the display server\n");
        return 1;
    }

    /* without /proc, lite-xl finds its data directory (/share/lite-xl) from argv[0] */
    int n = argc > 0 ? argc : 1;
    char *args[n + 1];
    args[0] = LITE_XL_EXE;
    for (int i = 1; i < n; i++)
        args[i] = argv[i];
    args[n] = NULL;

    setenv("LITE_USERDIR", LITE_XL_USERDIR, 0);

    return lite_xl_main(n, args);
}
