typedef struct
{
    int argc;
    char **argv;
    char *backing;
}
TaskArgs;

__attribute__((noreturn))
void _start(TaskArgs *args)
{
    extern int main(int argc, char **argv);
    extern __attribute__((noreturn)) void exit(int code);

    int argc = args ? args->argc : 0;
    char **argv = args ? args->argv : 0;

    exit(main(argc, argv));
}