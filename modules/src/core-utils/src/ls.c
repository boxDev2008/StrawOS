#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#define COL_RESET  "\033[0m"
#define COL_BOLD   "\033[1m"
#define COL_CYAN   "\033[36m"
#define COL_GREEN  "\033[32m"

static int has_lua_ext(const char *name)
{
    size_t len = strlen(name);
    return len >= 5 && strcmp(name + len - 4, ".lua") == 0;
}

int main(int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : ".";

    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: cannot open '%s'\n", path);
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_type == DIRENT_DIR)
            printf(COL_CYAN COL_BOLD "%s/" COL_RESET "\n", ent->d_name);
        else if (has_lua_ext(ent->d_name))
            printf(COL_GREEN "%s" COL_RESET "\n", ent->d_name);
        else
            printf("%s\n", ent->d_name);
    }
    closedir(d);
    return 0;
}