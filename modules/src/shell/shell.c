/*
 * StrawOS Shell (ssh)
 * -------------------
 * A Unix-like interactive shell for StrawOS whose native scripting
 * language is Lua.
 *
 * Command resolution order:
 *   1. Built-in commands (cd, pwd, ls, echo, cat, mkdir, rm, mv,
 *                         clear, help, exit, kill, ps, lua, source)
 *   2. Bare name treated as a path (relative or absolute):
 *        a. If the file starts with ELF magic → spawn as a task
 *        b. If the file ends in ".lua"        → run through Lua
 *   3. Anything else is evaluated as raw Lua code.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stat.h>
#include <dev.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/* ── constants ─────────────────────────────────────────────────────────── */

#define SHELL_NAME      "ssh"
#define OS_NAME         "StrawOS"
#define MAX_LINE        1024
#define MAX_ARGS        64
#define CWD_MAX         512
#define HISTORY_MAX     64

/* ELF magic: \x7fELF */
#define ELF_MAGIC0  0x7f
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'

/* ── globals ───────────────────────────────────────────────────────────── */

static lua_State *L       = NULL;
static int        running     = 1;
static int        skip_prompt = 0;  /* set by clear to avoid double prompt */

/* simple ring-buffer command history */
static char  history[HISTORY_MAX][MAX_LINE];
static int   history_count = 0;

/* ── colour helpers (ANSI escapes work through flanterm) ──────────────── */
#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_CYAN    "\033[36m"
#define COL_BLUE    "\033[34m"
#define COL_MAGENTA "\033[35m"
#define COL_WHITE   "\033[37m"

/* ── utility ───────────────────────────────────────────────────────────── */

static void history_push(const char *line)
{
    if (history_count < HISTORY_MAX) {
        strncpy(history[history_count], line, MAX_LINE - 1);
        history[history_count][MAX_LINE - 1] = '\0';
        history_count++;
    } else {
        /* shift ring */
        for (int i = 0; i < HISTORY_MAX - 1; i++)
            memcpy(history[i], history[i + 1], MAX_LINE);
        strncpy(history[HISTORY_MAX - 1], line, MAX_LINE - 1);
        history[HISTORY_MAX - 1][MAX_LINE - 1] = '\0';
    }
}

/* Build an absolute path from a possibly-relative @path and current dir. */
static void resolve_path(const char *path, char *out, size_t outsz)
{
    if (path[0] == '/') {
        strncpy(out, path, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }
    char cwd[CWD_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        strncpy(cwd, "/", sizeof(cwd));

    /* append */
    size_t cwdlen = strlen(cwd);
    if (cwdlen > 0 && cwd[cwdlen - 1] == '/')
        snprintf(out, outsz, "%s%s", cwd, path);
    else
        snprintf(out, outsz, "%s/%s", cwd, path);
}

/* Returns 1 if the first 4 bytes of @path are the ELF magic. */
static int is_elf(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    unsigned char magic[4] = {0};
    int n = read(fd, magic, 4);
    close(fd);
    if (n < 4) return 0;
    return magic[0] == ELF_MAGIC0 &&
           magic[1] == ELF_MAGIC1 &&
           magic[2] == ELF_MAGIC2 &&
           magic[3] == ELF_MAGIC3;
}

/* Returns 1 if @name ends with ".lua" (case-sensitive). */
static int has_lua_ext(const char *name)
{
    size_t len = strlen(name);
    if (len < 5) return 0;
    return strcmp(name + len - 4, ".lua") == 0;
}

/* Print a simple error in red. */
static void shell_err(const char *msg)
{
    fflush(stdout);
    fprintf(stdout, COL_RED "%s: %s" COL_RESET "\n", SHELL_NAME, msg);
    fflush(stdout);
}

static void shell_errf(const char *fmt, ...)
{
    fflush(stdout);
    fprintf(stdout, COL_RED "%s: " COL_RESET, SHELL_NAME);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

/* ── tokeniser ─────────────────────────────────────────────────────────── */
/*
 * Splits @line into argv[] tokens, respecting single and double quotes.
 * Returns argc.  The input buffer is modified in-place.
 */
static int tokenise(char *line, char *argv[], int maxargs)
{
    int argc = 0;
    char *p  = line;

    while (*p && argc < maxargs - 1) {
        /* skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') break;  /* end or comment */

        char *tok_start;
        if (*p == '"' || *p == '\'') {
            /* quoted token */
            char q = *p++;
            tok_start = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = '\0';
        } else {
            tok_start = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
        argv[argc++] = tok_start;
    }
    argv[argc] = NULL;
    return argc;
}

/* ── Lua integration ───────────────────────────────────────────────────── */

/* Execute a string of Lua code; prints errors to stderr. */
static int lua_exec_string(const char *code)
{
    int rc = luaL_dostring(L, code);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fflush(stdout);
        fprintf(stdout, COL_RED "lua: %s" COL_RESET "\n",
                err ? err : "(unknown error)");
        fflush(stdout);
        lua_pop(L, 1);
        return 1;
    }
    return 0;
}

/* Execute a .lua file; prints errors to stderr. */
static int lua_exec_file(const char *path)
{
    int rc = luaL_dofile(L, path);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fflush(stdout);
        fprintf(stdout, COL_RED "lua: %s" COL_RESET "\n",
                err ? err : "(unknown error)");
        fflush(stdout);
        lua_pop(L, 1);
        return 1;
    }
    return 0;
}

/*
 * Register StrawOS-specific functions into the Lua "os" table extension
 * and a top-level "shell" table.
 */

/* shell.spawn(path) → pid or nil */
static int l_spawn(lua_State *ls)
{
    const char *path = luaL_checkstring(ls, 1);
    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));
    int pid = spawn(abs);
    if (pid < 0) {
        lua_pushnil(ls);
        lua_pushstring(ls, "spawn failed");
        return 2;
    }
    lua_pushinteger(ls, pid);
    return 1;
}

/* shell.kill(pid) → true/false */
static int l_kill(lua_State *ls)
{
    int pid = (int)luaL_checkinteger(ls, 1);
    int rc  = kill(pid);
    lua_pushboolean(ls, rc == 0);
    return 1;
}

/* shell.getpid() → pid */
static int l_getpid(lua_State *ls)
{
    lua_pushinteger(ls, getpid());
    return 1;
}

/* shell.chdir(path) → true/false */
static int l_chdir(lua_State *ls)
{
    const char *path = luaL_checkstring(ls, 1);
    lua_pushboolean(ls, chdir(path) == 0);
    return 1;
}

/* shell.getcwd() → string */
static int l_getcwd(lua_State *ls)
{
    char buf[CWD_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        lua_pushnil(ls);
        return 1;
    }
    lua_pushstring(ls, buf);
    return 1;
}

/* shell.listdir(path) → table of {name, type} or nil */
static int l_listdir(lua_State *ls)
{
    const char *path = luaL_optstring(ls, 1, ".");
    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));

    DIR *d = opendir(abs);
    if (!d) { lua_pushnil(ls); return 1; }

    lua_newtable(ls);
    int idx = 1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        lua_newtable(ls);
        lua_pushstring(ls, ent->d_name);
        lua_setfield(ls, -2, "name");
        lua_pushstring(ls,
            ent->d_type == DIRENT_DIR  ? "dir" :
            ent->d_type == DIRENT_FILE ? "file" : "unknown");
        lua_setfield(ls, -2, "type");
        lua_rawseti(ls, -2, idx++);
    }
    closedir(d);
    return 1;
}

/* shell.read_file(path) → string or nil */
static int l_read_file(lua_State *ls)
{
    const char *path = luaL_checkstring(ls, 1);
    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));

    struct stat st;
    if (stat(abs, &st) < 0 || st.st_type != STAT_FILE) {
        lua_pushnil(ls); return 1;
    }

    FILE *f = fopen(abs, "r");
    if (!f) { lua_pushnil(ls); return 1; }

    char *buf = malloc(st.st_size + 1);
    if (!buf) { fclose(f); lua_pushnil(ls); return 1; }

    size_t n = fread(buf, 1, st.st_size, f);
    buf[n] = '\0';
    fclose(f);

    lua_pushlstring(ls, buf, n);
    free(buf);
    return 1;
}

/* shell.write_file(path, data) → true/false */
static int l_write_file(lua_State *ls)
{
    const char *path = luaL_checkstring(ls, 1);
    size_t len;
    const char *data = luaL_checklstring(ls, 2, &len);

    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));

    FILE *f = fopen(abs, "w");
    if (!f) { lua_pushboolean(ls, 0); return 1; }
    fwrite(data, 1, len, f);
    fclose(f);
    lua_pushboolean(ls, 1);
    return 1;
}

/* shell.stat(path) → table or nil */
static int l_stat(lua_State *ls)
{
    const char *path = luaL_checkstring(ls, 1);
    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));

    struct stat st;
    if (stat(abs, &st) < 0) { lua_pushnil(ls); return 1; }

    lua_newtable(ls);
    lua_pushinteger(ls, (lua_Integer)st.st_size);
    lua_setfield(ls, -2, "size");
    lua_pushstring(ls,
        st.st_type == STAT_DIR  ? "dir" :
        st.st_type == STAT_FILE ? "file" : "symlink");
    lua_setfield(ls, -2, "type");
    lua_pushinteger(ls, (lua_Integer)st.st_ino);
    lua_setfield(ls, -2, "ino");
    return 1;
}

/* shell.time() — return current PIT tick count (ms since boot) */
static int l_time(lua_State *ls)
{
    lua_pushinteger(ls, (lua_Integer)syscall0(SYS_TIME));
    return 1;
}
static const luaL_Reg shell_lib[] = {
    { "spawn",      l_spawn      },
    { "kill",       l_kill       },
    { "getpid",     l_getpid     },
    { "chdir",      l_chdir      },
    { "getcwd",     l_getcwd     },
    { "listdir",    l_listdir    },
    { "read_file",  l_read_file  },
    { "write_file", l_write_file },
    { "stat",       l_stat       },
    { "time",       l_time       },
    { NULL, NULL }
};

static void register_shell_lib(lua_State *ls)
{
    luaL_newlib(ls, shell_lib);
    lua_setglobal(ls, "shell");

    /* also expose os.spawn / os.kill on the standard os table */
    lua_getglobal(ls, "os");
    if (lua_istable(ls, -1)) {
        lua_pushcfunction(ls, l_spawn);  lua_setfield(ls, -2, "spawn");
        lua_pushcfunction(ls, l_kill);   lua_setfield(ls, -2, "kill");
        lua_pushcfunction(ls, l_getpid); lua_setfield(ls, -2, "getpid");
        lua_pushcfunction(ls, l_chdir);  lua_setfield(ls, -2, "chdir");
        lua_pushcfunction(ls, l_getcwd); lua_setfield(ls, -2, "getcwd");
    }
    lua_pop(ls, 1);
}

/* ── built-in commands ─────────────────────────────────────────────────── */

/* forward declarations */
static void print_prompt(void);
static void run_command(char *line);
static void tab_complete(char *buf, int *pos, int bufsz);

/* ------------------------------------------------------------------ cd */
static int builtin_cd(int argc, char *argv[])
{
    const char *dest = (argc >= 2) ? argv[1] : "/";
    if (chdir(dest) < 0)
        shell_errf("cd: cannot change to '%s'", dest);
    return 0;
}

/* ----------------------------------------------------------------- pwd */
static int builtin_pwd(int argc, char *argv[])
{
    (void)argc; (void)argv;
    char buf[CWD_MAX];
    if (!getcwd(buf, sizeof(buf)))
        strcpy(buf, "/");
    puts(buf);
    return 0;
}

/* ------------------------------------------------------------------ ls */
static int builtin_ls(int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : ".";
    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));

    DIR *d = opendir(abs);
    if (!d) {
        shell_errf("ls: cannot open '%s'", abs);
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
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

/* ----------------------------------------------------------------- cat */
static int builtin_cat(int argc, char *argv[])
{
    if (argc < 2) { shell_err("cat: missing operand"); return 1; }
    for (int i = 1; i < argc; i++) {
        char abs[CWD_MAX * 2];
        resolve_path(argv[i], abs, sizeof(abs));

        FILE *f = fopen(abs, "r");
        if (!f) { shell_errf("cat: cannot open '%s'", argv[i]); continue; }

        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            fwrite(buf, 1, n, stdout);
        fclose(f);
    }
    return 0;
}

/* ---------------------------------------------------------------- echo */
static int builtin_echo(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}

/* --------------------------------------------------------------- mkdir */
static int builtin_mkdir(int argc, char *argv[])
{
    if (argc < 2) { shell_err("mkdir: missing operand"); return 1; }
    for (int i = 1; i < argc; i++) {
        char abs[CWD_MAX * 2];
        resolve_path(argv[i], abs, sizeof(abs));
        if (mkdir(abs, 0) < 0)
            shell_errf("mkdir: cannot create '%s'", argv[i]);
    }
    return 0;
}

/* ------------------------------------------------------------------ rm */
static int builtin_rm(int argc, char *argv[])
{
    if (argc < 2) { shell_err("rm: missing operand"); return 1; }
    for (int i = 1; i < argc; i++) {
        char abs[CWD_MAX * 2];
        resolve_path(argv[i], abs, sizeof(abs));
        if (remove(abs) < 0)
            shell_errf("rm: cannot remove '%s'", argv[i]);
    }
    return 0;
}

/* ------------------------------------------------------------------ mv */
static int builtin_mv(int argc, char *argv[])
{
    if (argc < 3) { shell_err("mv: usage: mv <src> <dst>"); return 1; }
    char src[CWD_MAX * 2], dst[CWD_MAX * 2];
    resolve_path(argv[1], src, sizeof(src));
    resolve_path(argv[2], dst, sizeof(dst));
    if (rename(src, dst) < 0)
        shell_errf("mv: cannot rename '%s' to '%s'", argv[1], argv[2]);
    return 0;
}

/* --------------------------------------------------------------- clear */
static int builtin_clear(int argc, char *argv[])
{
    (void)argc; (void)argv;
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
    print_prompt();
    skip_prompt = 1;
    return 0;
}

/* ------------------------------------------------------------------ ps */
static int builtin_ps(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf(COL_BOLD "  PID  COMMAND" COL_RESET "\n");
    printf("  %d  %s\n", getpid(), SHELL_NAME);
    return 0;
}

/* --------------------------------------------------------------- kill  */
static int builtin_kill(int argc, char *argv[])
{
    if (argc < 2) { shell_err("kill: usage: kill <pid>"); return 1; }
    int pid = atoi(argv[1]);
    if (kill(pid) < 0)
        shell_errf("kill: failed to kill pid %d", pid);
    return 0;
}

/* --------------------------------------------------------------- lua   */
static int builtin_lua(int argc, char *argv[])
{
    if (argc < 2) { shell_err("lua: usage: lua <file.lua> | -e <code>"); return 1; }
    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 3) { shell_err("lua: -e requires an argument"); return 1; }
        return lua_exec_string(argv[2]);
    }
    char abs[CWD_MAX * 2];
    resolve_path(argv[1], abs, sizeof(abs));
    return lua_exec_file(abs);
}

/* ------------------------------------------------------------- source  */
static int builtin_source(int argc, char *argv[])
{
    if (argc < 2) { shell_err("source: usage: source <file>"); return 1; }
    char abs[CWD_MAX * 2];
    resolve_path(argv[1], abs, sizeof(abs));

    FILE *f = fopen(abs, "r");
    if (!f) { shell_errf("source: cannot open '%s'", argv[1]); return 1; }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        /* strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (len > 1 && line[len - 2] == '\r') line[len - 2] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        run_command(line);
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------- history */
static int builtin_history(int argc, char *argv[])
{
    (void)argc; (void)argv;
    for (int i = 0; i < history_count; i++)
        printf("  %3d  %s\n", i + 1, history[i]);
    return 0;
}

/* --------------------------------------------------------------- help  */
static int builtin_help(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf(COL_BOLD COL_CYAN
           "  " OS_NAME " Shell (" SHELL_NAME ")\n"
           COL_RESET
           "\n"
           COL_BOLD "  Built-in commands:\n" COL_RESET
           "    cd [dir]           Change working directory\n"
           "    pwd                Print working directory\n"
           "    ls [dir]           List directory contents\n"
           "    cat <file...>      Print file contents\n"
           "    echo [args...]     Print arguments\n"
           "    mkdir <dir...>     Create directories\n"
           "    rm <file...>       Remove files\n"
           "    mv <src> <dst>     Rename/move a file\n"
           "    clear              Clear the screen\n"
           "    ps                 List running tasks\n"
           "    kill <pid>         Kill a task by PID\n"
           "    lua <file>         Execute a Lua script\n"
           "    lua -e <code>      Evaluate inline Lua code\n"
           "    source <file>      Execute shell commands from file\n"
           "    history            Show command history\n"
           "    help               Show this help\n"
           "    exit [code]        Exit the shell\n"
           "\n"
           COL_BOLD "  Command resolution:\n" COL_RESET
           "    1. Built-in commands (above)\n"
           "    2. ELF binary at the given path → spawned as a task\n"
           "    3. .lua file at the given path  → executed by Lua\n"
           "    4. Anything else                → evaluated as Lua code\n"
           "\n"
           COL_BOLD "  Lua API (shell.*):\n" COL_RESET
           "    shell.spawn(path)          Spawn ELF task, returns pid\n"
           "    shell.kill(pid)            Kill task\n"
           "    shell.getpid()             PID of this shell\n"
           "    shell.chdir(path)          Change directory\n"
           "    shell.getcwd()             Get current directory\n"
           "    shell.listdir([path])      List directory → table\n"
           "    shell.read_file(path)      Read file → string\n"
           "    shell.write_file(path,s)   Write string to file\n"
           "    shell.stat(path)           Stat path → table\n"
    );
    return 0;
}

/* --------------------------------------------------------------- exit  */
static int builtin_exit(int argc, char *argv[])
{
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    running = 0;
    exit(code);
    return 0;
}

/* ── built-in dispatch table ───────────────────────────────────────────── */

typedef int (*builtin_fn)(int argc, char *argv[]);

typedef struct {
    const char *name;
    builtin_fn  fn;
} Builtin;

static const Builtin builtins[] = {
    { "cd",      builtin_cd      },
    { "pwd",     builtin_pwd     },
    { "ls",      builtin_ls      },
    { "cat",     builtin_cat     },
    { "echo",    builtin_echo    },
    { "mkdir",   builtin_mkdir   },
    { "rm",      builtin_rm      },
    { "mv",      builtin_mv      },
    { "clear",   builtin_clear   },
    { "ps",      builtin_ps      },
    { "kill",    builtin_kill    },
    { "lua",     builtin_lua     },
    { "source",  builtin_source  },
    { ".",       builtin_source  },  /* POSIX alias */
    { "history", builtin_history },
    { "help",    builtin_help    },
    { "exit",    builtin_exit    },
    { "quit",    builtin_exit    },
    { NULL, NULL }
};

/* ── command runner ────────────────────────────────────────────────────── */

static void run_command(char *line)
{
    /* make a mutable copy for the tokeniser */
    char buf[MAX_LINE];
    strncpy(buf, line, MAX_LINE - 1);
    buf[MAX_LINE - 1] = '\0';

    char *argv[MAX_ARGS];
    int   argc = tokenise(buf, argv, MAX_ARGS);
    if (argc == 0) return;

    const char *cmd = argv[0];

    /* 1. built-in? */
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            builtins[i].fn(argc, argv);
            return;
        }
    }

    /* 2a. does the name look like a path (contains '/' or starts with '.')?
     *     Or: does a file with that exact name exist?
     *     Either way, try ELF → Lua resolution on the resolved path.        */
    int try_as_file = (strchr(cmd, '/') != NULL);
    if (!try_as_file) {
        /* check bare name in cwd */
        char abs[CWD_MAX * 2];
        resolve_path(cmd, abs, sizeof(abs));
        struct stat st;
        if (stat(abs, &st) == 0 && st.st_type == STAT_FILE)
            try_as_file = 1;
    }

    if (try_as_file) {
        char abs[CWD_MAX * 2];
        resolve_path(cmd, abs, sizeof(abs));

        /* 2a-i. ELF binary */
        if (is_elf(abs)) {
            int pid = spawn(abs);
            if (pid < 0) {
                shell_errf("cannot execute '%s'", cmd);
            } else {
                /* Cooperative: yield until the child is done.
                 * The kernel marks spawned tasks DEAD when they exit;
                 * we poll by yielding a few times. A proper wait()
                 * syscall would be nicer but isn't in the kernel yet. */
                for (int i = 0; i < 256; i++) yield();
            }
            return;
        }

        /* 2a-ii. Lua file */
        if (has_lua_ext(abs)) {
            /* push remaining argv as a Lua global "arg" table */
            lua_newtable(L);
            lua_pushstring(L, abs);
            lua_rawseti(L, -2, 0);
            for (int i = 1; i < argc; i++) {
                lua_pushstring(L, argv[i]);
                lua_rawseti(L, -2, i);
            }
            lua_setglobal(L, "arg");

            lua_exec_file(abs);
            return;
        }

        shell_errf("'%s': not an ELF or Lua file", cmd);
        return;
    }

    /* 3. Does the line look like Lua code?
     *    We require at least one of: '(' '=' '.' '[' operators, or known
     *    Lua keywords at the start.  A bare word with no such markers is
     *    almost certainly a mistyped command, not Lua — report it clearly
     *    instead of crashing the VM with an invalid string dereference. */
    static const char *lua_keywords[] = {
        "local ", "if ", "while ", "for ", "do ", "function ",
        "return ", "print(", "require(", "not ", "true", "false",
        NULL
    };
    int looks_like_lua = 0;
    for (int i = 0; lua_keywords[i]; i++) {
        if (strncmp(line, lua_keywords[i], strlen(lua_keywords[i])) == 0) {
            looks_like_lua = 1;
            break;
        }
    }
    if (!looks_like_lua) {
        /* check for operator characters anywhere in the line */
        for (const char *p = line; *p; p++) {
            if (*p == '=' || *p == '(' || *p == '[' || *p == ':') {
                looks_like_lua = 1;
                break;
            }
        }
    }

    if (looks_like_lua) {
        lua_exec_string(line);
    } else {
        shell_errf("'%s': command not found", cmd);
    }
}

/* ── prompt ────────────────────────────────────────────────────────────── */

static void print_prompt(void)
{
    char cwd[CWD_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "/");

    /* shorten home: if cwd starts with /home or is just /, leave it */
    printf(COL_GREEN OS_NAME COL_RESET
           ":"
           COL_CYAN "%s" COL_RESET
           COL_BOLD " $ " COL_RESET,
           cwd);
    fflush(stdout);
}

/* ── PS/2 keyboard input ───────────────────────────────────────────────── */

/*
 * Set 1 scancode → ASCII translation tables.
 *
 * Index = scancode byte (make code, 0x00–0x58).
 * Value = ASCII character, or 0 for non-printable / ignored keys.
 *
 * Scancodes >= 0x80 are break (key-release) codes — we ignore them.
 */

/* unshifted */
static const char sc_normal[128] = {
    /*00*/  0,    0,   '1', '2', '3', '4', '5', '6',
    /*08*/ '7',  '8', '9', '0', '-', '=',  '\b', '\t',
    /*10*/ 'q',  'w', 'e', 'r', 't', 'y', 'u',  'i',
    /*18*/ 'o',  'p', '[', ']', '\n',  0,  'a',  's',
    /*20*/ 'd',  'f', 'g', 'h', 'j',  'k', 'l',  ';',
    /*28*/ '\'',  '`',  0,  '\\', 'z', 'x', 'c', 'v',
    /*30*/ 'b',  'n', 'm', ',', '.', '/',   0,  '*',
    /*38*/  0,   ' ',  0,   0,   0,   0,   0,   0,
    /*40*/  0,    0,   0,   0,   0,   0,   0,  '7',
    /*48*/ '8',  '9', '-', '4', '5', '6', '+', '1',
    /*50*/ '2',  '3', '0', '.',  0,   0,   0,   0,
    /*58*/  0,    0,   0,   0,   0,   0,   0,   0,
    /* pad the rest to 128 */
    [64 ... 127] = 0
};

/* shifted */
static const char sc_shifted[128] = {
    /*00*/  0,    0,   '!', '@', '#', '$', '%', '^',
    /*08*/ '&',  '*', '(', ')', '_', '+', '\b', '\t',
    /*10*/ 'Q',  'W', 'E', 'R', 'T', 'Y', 'U',  'I',
    /*18*/ 'O',  'P', '{', '}', '\n',  0,  'A',  'S',
    /*20*/ 'D',  'F', 'G', 'H', 'J',  'K', 'L',  ':',
    /*28*/ '"',  '~',  0,  '|', 'Z', 'X', 'C', 'V',
    /*30*/ 'B',  'N', 'M', '<', '>', '?',  0,  '*',
    /*38*/  0,   ' ',  0,   0,   0,   0,   0,   0,
    [64 ... 127] = 0
};

/* special scancode values */
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_LSHIFT_R 0xAA   /* break */
#define SC_RSHIFT_R 0xB6   /* break */
#define SC_CAPS     0x3A
#define SC_CTRL     0x1D
#define SC_CTRL_R   0x9D
#define SC_ALT      0x38
#define SC_ALT_R    0xB8
#define SC_UP       0x48
#define SC_DOWN     0x50
#define SC_LEFT     0x4B
#define SC_RIGHT    0x4D
#define SC_DELETE   0x53
#define SC_HOME     0x47
#define SC_END      0x4F
#define SC_F1       0x3B   /* F1–F10: 0x3B–0x44 */

/* keyboard modifier state */
static int kb_shift   = 0;
static int kb_caps    = 0;
static int kb_ctrl    = 0;

/*
 * Poll the PS/2 keyboard queue for one decoded character.
 * Returns:
 *   > 0  : a printable ASCII character
 *   '\n' : Enter pressed
 *   '\b' : Backspace pressed
 *   '\t' : Tab pressed
 *   3    : Ctrl-C
 *   4    : Ctrl-D  (EOF / exit)
 *  -1    : special / non-character key (arrow keys etc.)
 *   0    : nothing in queue
 *
 * Modifier state (shift, caps, ctrl) is updated as a side-effect.
 */
static int kb_poll_char(void)
{
    uint8_t sc = 0;
    if (!device(DEVICE_PS2KEYBOARD, &sc))
        return 0;                        /* queue empty */

    /* break codes → update modifiers, return -1 */
    if (sc & 0x80) {
        uint8_t make = sc & 0x7F;
        if (make == SC_LSHIFT || make == SC_RSHIFT) kb_shift = 0;
        if (make == SC_CTRL)                         kb_ctrl  = 0;
        return -1;
    }

    /* make codes — modifiers */
    if (sc == SC_LSHIFT || sc == SC_RSHIFT) { kb_shift = 1; return -1; }
    if (sc == SC_CTRL)                       { kb_ctrl  = 1; return -1; }
    if (sc == SC_ALT)                        {               return -1; }
    if (sc == SC_CAPS) {
        kb_caps ^= 1;
        return -1;
    }

    /* arrow / function keys — return -1 (caller may use history etc.) */
    if (sc == SC_UP || sc == SC_DOWN || sc == SC_LEFT || sc == SC_RIGHT)
        return -(int)sc;   /* encode direction in sign+magnitude */
    if (sc >= SC_F1 && sc <= 0x44) return -1;

    /* look up ASCII */
    if (sc >= 128) return -1;
    char c = kb_shift ? sc_shifted[sc] : sc_normal[sc];
    if (c == 0) return -1;

    /* caps lock: flip case of letters */
    if (kb_caps && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (kb_caps && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';

    /* ctrl combos: Ctrl-C → 3, Ctrl-D → 4, Ctrl-L → 12 (clear), etc. */
    if (kb_ctrl) {
        if (c >= 'a' && c <= 'z') return c - 'a' + 1;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
        return -1;
    }

    return (unsigned char)c;
}

/*
 * Block until a character is available from the PS/2 keyboard.
 * Yields the CPU between polls to be cooperative.
 * Returns the decoded character (same semantics as kb_poll_char, but
 * never 0).
 */
static int kb_getchar(void)
{
    int c;
    while ((c = kb_poll_char()) == 0)
        yield();
    return c;
}

/* ── tab completion ────────────────────────────────────────────────────── */

#define COMPLETE_MAX 64

/* persistent state across consecutive Tab presses */
static char  tc_matches[COMPLETE_MAX][256];
static int   tc_is_dir[COMPLETE_MAX];
static int   tc_nmatch     = 0;
static int   tc_index      = -1;  /* currently inserted candidate */
static int   tc_word_start = 0;   /* where in buf the word begins */
static char  tc_partial[256];     /* original typed prefix */
static char  tc_dir_prefix[CWD_MAX * 2]; /* dir part before partial */

/* reset when any non-Tab key is pressed */
static void tab_reset(void)
{
    tc_nmatch = 0;
    tc_index  = -1;
}

static void tab_complete(char *buf, int *pos, int bufsz)
{
    buf[*pos] = '\0';

    /* ── first Tab in a sequence: collect candidates ────────────────── */
    if (tc_nmatch == 0) {
        /* find start of current word */
        int ws = *pos;
        while (ws > 0 && buf[ws-1] != ' ' && buf[ws-1] != '\t') ws--;
        tc_word_start = ws;

        const char *word = buf + ws;

        /* split into dir-prefix and partial name */
        const char *last_slash = strrchr(word, '/');
        if (last_slash) {
            size_t dl = (size_t)(last_slash - word) + 1;
            if (dl >= sizeof(tc_dir_prefix)) return;
            strncpy(tc_dir_prefix, word, dl);
            tc_dir_prefix[dl] = '\0';
            strncpy(tc_partial, last_slash + 1, sizeof(tc_partial) - 1);
        } else {
            tc_dir_prefix[0] = '\0';
            strncpy(tc_partial, word, sizeof(tc_partial) - 1);
        }
        tc_partial[sizeof(tc_partial) - 1] = '\0';
        size_t plen = strlen(tc_partial);

        /* resolve directory to scan */
        char abs_dir[CWD_MAX * 2];
        if (tc_dir_prefix[0] == '\0') {
            if (!getcwd(abs_dir, sizeof(abs_dir))) strcpy(abs_dir, "/");
        } else {
            resolve_path(tc_dir_prefix, abs_dir, sizeof(abs_dir));
        }

        /* scan filesystem */
        DIR *d = opendir(abs_dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL && tc_nmatch < COMPLETE_MAX) {
                if (plen > 0 &&
                    strncmp(ent->d_name, tc_partial, plen) != 0)
                    continue;
                strncpy(tc_matches[tc_nmatch], ent->d_name, 255);
                tc_matches[tc_nmatch][255] = '\0';
                tc_is_dir[tc_nmatch] = (ent->d_type == DIRENT_DIR);
                tc_nmatch++;
            }
            closedir(d);
        }

        /* add builtins when completing the first token */
        if (tc_dir_prefix[0] == '\0' && tc_word_start == 0) {
            for (int i = 0; builtins[i].name && tc_nmatch < COMPLETE_MAX; i++) {
                if (plen > 0 &&
                    strncmp(builtins[i].name, tc_partial, plen) != 0)
                    continue;
                int dup = 0;
                for (int j = 0; j < tc_nmatch; j++)
                    if (strcmp(tc_matches[j], builtins[i].name) == 0)
                        { dup = 1; break; }
                if (!dup) {
                    strncpy(tc_matches[tc_nmatch], builtins[i].name, 255);
                    tc_matches[tc_nmatch][255] = '\0';
                    tc_is_dir[tc_nmatch] = 0;
                    tc_nmatch++;
                }
            }
        }

        if (tc_nmatch == 0) {
            putchar('\a'); fflush(stdout);
            return;
        }

        tc_index = -1;
    }

    /* ── cycle to next candidate ────────────────────────────────────── */
    tc_index = (tc_index + 1) % tc_nmatch;

    /* ── erase whatever is currently in the word slot ───────────────── */
    int erase = *pos - tc_word_start;
    for (int i = 0; i < erase; i++) fputs("\b \b", stdout);
    *pos = tc_word_start;

    /* ── write dir_prefix + candidate (+ '/' for dirs) ─────────────── */
    size_t dp_len = strlen(tc_dir_prefix);
    for (size_t i = 0; i < dp_len && *pos < bufsz - 1; i++)
        buf[(*pos)++] = tc_dir_prefix[i];

    const char *name = tc_matches[tc_index];
    size_t nlen = strlen(name);
    for (size_t i = 0; i < nlen && *pos < bufsz - 1; i++)
        buf[(*pos)++] = name[i];

    if (tc_is_dir[tc_index] && *pos < bufsz - 1)
        buf[(*pos)++] = '/';

    buf[*pos] = '\0';
    fputs(buf + tc_word_start, stdout);
    fflush(stdout);
}

/* ── line reader ───────────────────────────────────────────────────────── */
/*
 * Read a line of input from the PS/2 keyboard into buf[0..bufsz-1].
 * Handles:
 *   - Printable characters with echo
 *   - Backspace / DEL
 *   - Ctrl-C  → discard line, return empty string
 *   - Ctrl-D  → return -1 (EOF / exit shell)
 *   - Ctrl-L  → clear screen, reprint prompt (handled in place)
 *   - Up/Down → basic history navigation
 *   - Enter   → commit line
 */
static int read_line(char *buf, int bufsz)
{
    int pos = 0;
    int hist_idx = history_count;   /* start "after" last entry */

    buf[0] = '\0';

    while (1) {
        int c = kb_getchar();

        /* ── Ctrl sequences ────────────────────────────────────────── */
        if (c == 3) {                        /* Ctrl-C */
            tab_reset();
            fputs("^C\n", stdout);
            fflush(stdout);
            buf[0] = '\0';
            return 0;
        }
        if (c == 4) {                        /* Ctrl-D */
            tab_reset();
            putchar('\n');
            return -1;
        }
        if (c == 12) {                       /* Ctrl-L: clear screen */
            tab_reset();
            fputs("\033[2J\033[H", stdout);
            print_prompt();
            fwrite(buf, 1, pos, stdout);
            fflush(stdout);
            continue;
        }

        /* ── History navigation (up/down arrow) ────────────────────── */
        if (c == -(int)SC_UP) {
            tab_reset();
            if (hist_idx > 0) {
                hist_idx--;
                /* erase current line on terminal */
                for (int i = 0; i < pos; i++) fputs("\b \b", stdout);
                strncpy(buf, history[hist_idx], bufsz - 1);
                buf[bufsz - 1] = '\0';
                pos = (int)strlen(buf);
                fputs(buf, stdout);
                fflush(stdout);
            }
            continue;
        }
        if (c == -(int)SC_DOWN) {
            tab_reset();
            /* erase current line */
            for (int i = 0; i < pos; i++) fputs("\b \b", stdout);
            if (hist_idx < history_count - 1) {
                hist_idx++;
                strncpy(buf, history[hist_idx], bufsz - 1);
                buf[bufsz - 1] = '\0';
            } else {
                hist_idx = history_count;
                buf[0] = '\0';
            }
            pos = (int)strlen(buf);
            fputs(buf, stdout);
            fflush(stdout);
            continue;
        }

        /* ── ignore other special keys ─────────────────────────────── */
        if (c < 0) continue;

        /* ── Enter ─────────────────────────────────────────────────── */
        if (c == '\n' || c == '\r') {
            tab_reset();
            buf[pos] = '\0';
            putchar('\n');
            fflush(stdout);
            return pos;
        }

        /* ── Backspace ─────────────────────────────────────────────── */
        if (c == '\b') {
            tab_reset();
            if (pos > 0) {
                pos--;
                fputs("\b \b", stdout);
                fflush(stdout);
            }
            continue;
        }

        /* ── Tab completion ─────────────────────────────────────────── */
        if (c == '\t') {
            buf[pos] = '\0';
            tab_complete(buf, &pos, bufsz);
            continue;
        }

        /* ── Printable character ───────────────────────────────────── */
        if (c >= 0x20 && pos < bufsz - 1) {
            tab_reset();
            buf[pos++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
}

/* ── banner ────────────────────────────────────────────────────────────── */

static void print_banner(void)
{
    printf(COL_CYAN COL_BOLD
        "\n"
        "  ____  _                     ___  ____\n"
        " / ___|| |_ _ __ __ ___      / _ \\/ ___|\n"
        " \\___ \\| __| '__/ _` \\ \\ /\\ / / | \\___ \\\n"
        "  ___) | |_| | | (_| |\\ V  V /| |_|___) |\n"
        " |____/ \\__|_|  \\__,_| \\_/\\_/  \\___/____/\n"
        COL_RESET
        "\n"
        "  " OS_NAME " — Lua-native shell.  Type " COL_BOLD "help" COL_RESET " for commands.\n"
        "\n");
}

/* ── init rc file ──────────────────────────────────────────────────────── */

static void run_rc(void)
{
    /* Try /etc/sshrc, then ~/.sshrc */
    const char *paths[] = { "/etc/sshrc", "/home/.sshrc", NULL };
    for (int i = 0; paths[i]; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && st.st_type == STAT_FILE) {
            char copy[64];
            strncpy(copy, paths[i], sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            char *argv[] = { "source", copy, NULL };
            builtin_source(2, argv);
        }
    }
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    /* ── Lua VM ── */
    L = luaL_newstate();
    if (!L) {
        fputs("ssh: failed to create Lua state\n", stderr);
        return 1;
    }
    /* Open only the safe Lua standard libs.
     * liolib (io.*) and loslib (os.*) make host OS calls that fault in
     * this freestanding environment — we provide our own via shell.*   */
    luaL_requiref(L, "_G",        luaopen_base,      1); lua_pop(L, 1);
    luaL_requiref(L, "math",      luaopen_math,      1); lua_pop(L, 1);
    luaL_requiref(L, "string",    luaopen_string,    1); lua_pop(L, 1);
    luaL_requiref(L, "table",     luaopen_table,     1); lua_pop(L, 1);
    luaL_requiref(L, "utf8",      luaopen_utf8,      1); lua_pop(L, 1);
    luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
    register_shell_lib(L);

    print_banner();

    /* set initial cwd */
    if (chdir("/") < 0)
        chdir("/modules");   /* fallback if / doesn't work */

    run_rc();

    /* ── REPL ── */
    char line[MAX_LINE];
    while (running) {
        if (skip_prompt) {
            skip_prompt = 0;
        } else {
            print_prompt();
        }

        int n = read_line(line, sizeof(line));
        if (n < 0) {
            /* EOF (Ctrl-D) */
            putchar('\n');
            break;
        }
        if (n == 0) continue;               /* empty line */
        if (line[0] == '#') continue;       /* comment */

        history_push(line);
        run_command(line);
    }

    lua_close(L);
    return 0;
}