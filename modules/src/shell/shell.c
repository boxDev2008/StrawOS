/*
 * StrawOS Shell (ssh)
 * -------------------
 * A Unix-like interactive shell for StrawOS whose native scripting
 * language is Lua.
 *
 * Command resolution order:
 *   1. Shell-internal built-ins that need VM / history state:
 *        source . lua history help exit quit
 *   2. External coreutils looked up in UTILS_PATH (spawned as ELF tasks
 *        with the full argv passed through).
 *   3. Bare name / path on the filesystem:
 *        a. ELF magic → spawn as a task (argv forwarded)
 *        b. ".lua" extension → run through the Lua VM
 *   4. Lua fallback: line evaluated as raw Lua code.
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

#include "syscall.h"

/* ── constants ─────────────────────────────────────────────────────────── */

#define SHELL_NAME   "ssh"
#define OS_NAME      "StrawOS"
#define MAX_LINE     1024
#define MAX_ARGS     64
#define CWD_MAX      512
#define HISTORY_MAX  64

/*
 * Directory that contains the external coreutil ELFs.
 * The shell looks for <UTILS_PATH>/<cmd>.elf before anything else.
 */
#define UTILS_PATH   "/modules/bin"

/* ELF magic: \x7fELF */
#define ELF_MAGIC0  0x7f
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'

/* ── globals ───────────────────────────────────────────────────────────── */

static lua_State *L           = NULL;
static int        running     = 1;
static int        skip_prompt = 0;

static char history[HISTORY_MAX][MAX_LINE];
static int  history_count = 0;

/* ── colour helpers ────────────────────────────────────────────────────── */
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
        for (int i = 0; i < HISTORY_MAX - 1; i++)
            memcpy(history[i], history[i + 1], MAX_LINE);
        strncpy(history[HISTORY_MAX - 1], line, MAX_LINE - 1);
        history[HISTORY_MAX - 1][MAX_LINE - 1] = '\0';
    }
}

/* Build an absolute path from a possibly-relative path and cwd. */
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

    size_t cwdlen = strlen(cwd);
    if (cwdlen > 0 && cwd[cwdlen - 1] == '/')
        snprintf(out, outsz, "%s%s", cwd, path);
    else
        snprintf(out, outsz, "%s/%s", cwd, path);
}

/* Returns 1 if the first 4 bytes of path are the ELF magic. */
static int is_elf(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    unsigned char magic[4] = {0};
    int n = read(fd, magic, 4);
    close(fd);
    if (n < 4) return 0;
    return magic[0] == ELF_MAGIC0 && magic[1] == ELF_MAGIC1 &&
           magic[2] == ELF_MAGIC2 && magic[3] == ELF_MAGIC3;
}

/* Returns 1 if name ends with ".lua" (case-sensitive). */
static int has_lua_ext(const char *name)
{
    size_t len = strlen(name);
    return len >= 5 && strcmp(name + len - 4, ".lua") == 0;
}

static void shell_err(const char *msg)
{
    fprintf(stdout, COL_RED "%s: %s" COL_RESET "\n", SHELL_NAME, msg);
    fflush(stdout);
}

static void shell_errf(const char *fmt, ...)
{
    fprintf(stdout, COL_RED "%s: " COL_RESET, SHELL_NAME);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

/* ── tokeniser ─────────────────────────────────────────────────────────── */

static int tokenise(char *line, char *argv[], int maxargs)
{
    int argc = 0;
    char *p  = line;

    while (*p && argc < maxargs - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') break;

        char *tok_start;
        if (*p == '"' || *p == '\'') {
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

/* ── spawn helpers ─────────────────────────────────────────────────────── */

/*
 * Resolve a bare command name to an ELF path inside UTILS_PATH.
 *
 * Search order:
 *   1. UTILS_PATH/<cmd>          — exact name (e.g. "ls.elf" typed as-is,
 *                                   or any ELF dropped in without extension)
 *   2. UTILS_PATH/<cmd>.elf      — name without extension (most common case:
 *                                   user types "ls", finds "ls.elf")
 *
 * Returns 1 and fills `out` (size outsz) if found, 0 otherwise.
 */
static int find_util(const char *cmd, char *out, size_t outsz)
{
    /* Don't mangle explicit paths — caller handles those. */
    if (strchr(cmd, '/')) return 0;

    /* 1. <UTILS_PATH>/<cmd>.elf */
    snprintf(out, outsz, "%s/%s.elf", UTILS_PATH, cmd);
    if (is_elf(out)) return 1;

    /* 2. <UTILS_PATH>/<cmd> (no extension) */
    snprintf(out, outsz, "%s/%s", UTILS_PATH, cmd);
    if (is_elf(out)) return 1;

    return 0;
}

/*
 * Try to find and run a coreutil for `cmd`.
 * Searches UTILS_PATH for the ELF and spawns it with the full argv.
 * Returns 1 if the util was found and launched, 0 if not found.
 */
static int try_spawn_util(const char *cmd, int argc, char *argv[])
{
    char path[CWD_MAX * 2];
    if (!find_util(cmd, path, sizeof(path)))
        return 0;

    /* Build a const argv — argv[0] is the bare command name as typed. */
    const char *sargv[MAX_ARGS + 1];
    sargv[0] = cmd;
    for (int i = 1; i < argc && i < MAX_ARGS; i++)
        sargv[i] = argv[i];
    sargv[argc] = NULL;

    int pid = spawn(path, sargv);
    if (pid < 0) {
        shell_errf("cannot execute '%s'", cmd);
        return 1; /* found but failed — don't fall through */
    }

    while (waitpid((uint32_t)pid) != 0);
    return 1;
}

/*
 * Spawn an arbitrary ELF at `abs_path` with the full argv from the command
 * line (argv[0] is the command as typed, rest are the user's arguments).
 */
static void spawn_elf(const char *abs_path, int argc, char *argv[])
{
    const char *sargv[MAX_ARGS + 1];
    for (int i = 0; i < argc && i < MAX_ARGS; i++)
        sargv[i] = argv[i];
    sargv[argc] = NULL;

    int pid = spawn(abs_path, sargv);
    if (pid < 0) {
        shell_errf("cannot execute '%s'", argv[0]);
        return;
    }
    while (waitpid((uint32_t)pid) != 0) ;
}

/* ── Lua integration ───────────────────────────────────────────────────── */

static int lua_exec_string(const char *code)
{
    int rc = luaL_dostring(L, code);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stdout, COL_RED "lua: %s" COL_RESET "\n",
                err ? err : "(unknown error)");
        fflush(stdout);
        lua_pop(L, 1);
        return 1;
    }
    return 0;
}

static int lua_exec_file(const char *path)
{
    int rc = luaL_dofile(L, path);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stdout, COL_RED "lua: %s" COL_RESET "\n",
                err ? err : "(unknown error)");
        fflush(stdout);
        lua_pop(L, 1);
        return 1;
    }
    return 0;
}

/* ── Lua shell.* library ───────────────────────────────────────────────── */

/* shell.spawn(path [, arg1, arg2, ...]) → pid or nil, errmsg */
static int l_spawn(lua_State *ls)
{
    const char *path = luaL_checkstring(ls, 1);
    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));

    /* collect extra string args from the Lua call */
    int extra = lua_gettop(ls) - 1;
    const char *sargv[MAX_ARGS + 1];
    sargv[0] = path;
    for (int i = 0; i < extra && i < MAX_ARGS - 1; i++)
        sargv[i + 1] = luaL_checkstring(ls, i + 2);
    sargv[extra + 1] = NULL;

    int pid = spawn(abs, sargv);
    if (pid < 0) {
        lua_pushnil(ls);
        lua_pushstring(ls, "spawn failed");
        return 2;
    }
    lua_pushinteger(ls, pid);
    return 1;
}

static int l_kill(lua_State *ls)
{
    int pid = (int)luaL_checkinteger(ls, 1);
    lua_pushboolean(ls, kill(pid) == 0);
    return 1;
}

static int l_getpid(lua_State *ls)
{
    lua_pushinteger(ls, getpid());
    return 1;
}

static int l_chdir(lua_State *ls)
{
    lua_pushboolean(ls, chdir(luaL_checkstring(ls, 1)) == 0);
    return 1;
}

static int l_getcwd(lua_State *ls)
{
    char buf[CWD_MAX];
    if (!getcwd(buf, sizeof(buf))) { lua_pushnil(ls); return 1; }
    lua_pushstring(ls, buf);
    return 1;
}

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
        lua_pushstring(ls, ent->d_name);     lua_setfield(ls, -2, "name");
        lua_pushstring(ls, ent->d_type == DIRENT_DIR  ? "dir"  :
                           ent->d_type == DIRENT_FILE ? "file" : "unknown");
        lua_setfield(ls, -2, "type");
        lua_rawseti(ls, -2, idx++);
    }
    closedir(d);
    return 1;
}

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

static int l_stat(lua_State *ls)
{
    const char *path = luaL_checkstring(ls, 1);
    char abs[CWD_MAX * 2];
    resolve_path(path, abs, sizeof(abs));

    struct stat st;
    if (stat(abs, &st) < 0) { lua_pushnil(ls); return 1; }

    lua_newtable(ls);
    lua_pushinteger(ls, (lua_Integer)st.st_size); lua_setfield(ls, -2, "size");
    lua_pushstring(ls, st.st_type == STAT_DIR  ? "dir"     :
                       st.st_type == STAT_FILE ? "file"    : "symlink");
    lua_setfield(ls, -2, "type");
    lua_pushinteger(ls, (lua_Integer)st.st_ino);  lua_setfield(ls, -2, "ino");
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
    { NULL, NULL }
};

static void register_shell_lib(lua_State *ls)
{
    luaL_newlib(ls, shell_lib);
    lua_setglobal(ls, "shell");

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

/* ── shell-internal built-ins ──────────────────────────────────────────── */
/*
 * Only commands that need access to shell-internal state (Lua VM, history
 * ring, running flag) live here.  Everything else is an external ELF.
 */

static void run_command(char *line); /* forward */
static void print_prompt(void);      /* forward */

/* source / . */
static int builtin_source(int argc, char *argv[])
{
    if (argc < 2) { shell_err("source: usage: source <file>"); return 1; }
    char abs[CWD_MAX * 2];
    resolve_path(argv[1], abs, sizeof(abs));

    FILE *f = fopen(abs, "r");
    if (!f) { shell_errf("source: cannot open '%s'", argv[1]); return 1; }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len-1] == '\r') line[--len] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        run_command(line);
    }
    fclose(f);
    return 0;
}

/* lua */
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

/* history */
static int builtin_history(int argc, char *argv[])
{
    (void)argc; (void)argv;
    for (int i = 0; i < history_count; i++)
        printf("  %3d  %s\n", i + 1, history[i]);
    return 0;
}

/* help */
static int builtin_help(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf(COL_BOLD COL_CYAN
           "  " OS_NAME " Shell (" SHELL_NAME ")\n"
           COL_RESET "\n"
           COL_BOLD "  Shell built-ins:\n" COL_RESET
           "    lua <file>         Execute a Lua script\n"
           "    lua -e <code>      Evaluate inline Lua code\n"
           "    source <file>      Execute shell commands from a file\n"
           "    history            Show command history\n"
           "    help               Show this help\n"
           "    exit [code]        Exit the shell\n"
           "\n"
           COL_BOLD "  Core utilities (external ELFs in " UTILS_PATH "):\n" COL_RESET
           "    cd [dir]           Change working directory\n"
           "    pwd                Print working directory\n"
           "    ls [dir]           List directory contents\n"
           "    cat <file...>      Print file contents\n"
           "    echo [-n] [args]   Print arguments\n"
           "    mkdir <dir...>     Create directories\n"
           "    rm <file...>       Remove files\n"
           "    mv <src> <dst>     Rename/move a file\n"
           "    clear              Clear the screen\n"
           "    ps                 List running tasks\n"
           "    kill <pid>         Kill a task by PID\n"
           "\n"
           COL_BOLD "  Command resolution:\n" COL_RESET
           "    1. Shell built-ins\n"
           "    2. " UTILS_PATH "/<cmd>.elf  (core utilities, args forwarded)\n"
           "    3. ELF binary at the given path (args forwarded)\n"
           "    4. .lua file at the given path\n"
           "    5. Raw Lua expression\n"
           "\n"
           COL_BOLD "  Lua API (shell.*):\n" COL_RESET
           "    shell.spawn(path, ...)     Spawn ELF, returns pid\n"
           "    shell.kill(pid)            Kill task\n"
           "    shell.getpid()             PID of this shell\n"
           "    shell.chdir(path)          Change directory\n"
           "    shell.getcwd()             Get current directory\n"
           "    shell.listdir([path])      List directory → table\n"
           "    shell.read_file(path)      Read file → string\n"
           "    shell.write_file(path, s)  Write string to file\n"
           "    shell.stat(path)           Stat path → table\n"
           "    shell.time()               PIT ticks since boot\n"
    );
    return 0;
}

/* exit / quit */
static int builtin_exit(int argc, char *argv[])
{
    int code = (argc >= 2) ? atoi(argv[1]) : 0;
    running = 0;
    exit(code);
    return 0;
}

/* cd — must be a built-in: a child process cannot change the shell's cwd */
static int builtin_cd(int argc, char *argv[])
{
    const char *dest = (argc >= 2) ? argv[1] : "/";
    if (chdir(dest) < 0)
        shell_errf("cd: cannot change to '%s'", dest);
    return 0;
}

/* ── internal built-in dispatch table ──────────────────────────────────── */

typedef int (*builtin_fn)(int argc, char *argv[]);
typedef struct { const char *name; builtin_fn fn; } Builtin;

static const Builtin builtins[] = {
    { "cd",      builtin_cd      },
    { "lua",     builtin_lua     },
    { "source",  builtin_source  },
    { ".",       builtin_source  },
    { "history", builtin_history },
    { "help",    builtin_help    },
    { "exit",    builtin_exit    },
    { "quit",    builtin_exit    },
    { NULL, NULL }
};

/* ── command runner ────────────────────────────────────────────────────── */

static void run_command(char *line)
{
    char buf[MAX_LINE];
    strncpy(buf, line, MAX_LINE - 1);
    buf[MAX_LINE - 1] = '\0';

    char *argv[MAX_ARGS];
    int   argc = tokenise(buf, argv, MAX_ARGS);
    if (argc == 0) return;

    const char *cmd = argv[0];

    /* 1. Shell-internal built-ins */
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            builtins[i].fn(argc, argv);
            return;
        }
    }

    /* 2. Core utility ELF: <UTILS_PATH>/<cmd>.elf */
    if (try_spawn_util(cmd, argc, argv))
        return;

    /* 3. Explicit path or bare name that exists as a file in cwd */
    int try_as_file = (strchr(cmd, '/') != NULL);
    if (!try_as_file) {
        char abs[CWD_MAX * 2];
        resolve_path(cmd, abs, sizeof(abs));
        struct stat st;
        if (stat(abs, &st) == 0 && st.st_type == STAT_FILE)
            try_as_file = 1;
    }

    if (try_as_file) {
        char abs[CWD_MAX * 2];
        resolve_path(cmd, abs, sizeof(abs));

        /* 3a. ELF binary — spawn with full argv */
        if (is_elf(abs)) {
            spawn_elf(abs, argc, argv);
            return;
        }

        /* 3b. Lua file */
        if (has_lua_ext(abs)) {
            lua_newtable(L);
            lua_pushstring(L, abs); lua_rawseti(L, -2, 0);
            for (int i = 1; i < argc; i++) {
                lua_pushstring(L, argv[i]); lua_rawseti(L, -2, i);
            }
            lua_setglobal(L, "arg");
            lua_exec_file(abs);
            return;
        }

        shell_errf("'%s': not an ELF or Lua file", cmd);
        return;
    }

    /* 4. Lua fallback: does the line look like Lua? */
    static const char *lua_keywords[] = {
        "local ", "if ", "while ", "for ", "do ", "function ",
        "return ", "print(", "require(", "not ", "true", "false",
        NULL
    };
    int looks_like_lua = 0;
    for (int i = 0; lua_keywords[i]; i++) {
        if (strncmp(line, lua_keywords[i], strlen(lua_keywords[i])) == 0) {
            looks_like_lua = 1; break;
        }
    }
    if (!looks_like_lua) {
        for (const char *p = line; *p; p++) {
            if (*p == '=' || *p == '(' || *p == '[' || *p == ':') {
                looks_like_lua = 1; break;
            }
        }
    }

    if (looks_like_lua)
        lua_exec_string(line);
    else
        shell_errf("'%s': command not found", cmd);
}

/* ── prompt ────────────────────────────────────────────────────────────── */

static void print_prompt(void)
{
    char cwd[CWD_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "/");
    printf(COL_GREEN OS_NAME COL_RESET ":"
           COL_CYAN "%s" COL_RESET
           COL_BOLD " $ " COL_RESET, cwd);
    fflush(stdout);
}

/* ── PS/2 keyboard ─────────────────────────────────────────────────────── */

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
    [64 ... 127] = 0
};

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

#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CAPS     0x3A
#define SC_CTRL     0x1D
#define SC_ALT      0x38
#define SC_UP       0x48
#define SC_DOWN     0x50
#define SC_LEFT     0x4B
#define SC_RIGHT    0x4D
#define SC_F1       0x3B

static int kb_shift = 0;
static int kb_caps  = 0;
static int kb_ctrl  = 0;

static int kb_poll_char(void)
{
    uint8_t sc = 0;
    if (!device(DEVICE_PS2KEYBOARD, &sc)) return 0;

    if (sc & 0x80) {
        uint8_t make = sc & 0x7F;
        if (make == SC_LSHIFT || make == SC_RSHIFT) kb_shift = 0;
        if (make == SC_CTRL)                         kb_ctrl  = 0;
        return -1;
    }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT) { kb_shift = 1; return -1; }
    if (sc == SC_CTRL)                       { kb_ctrl  = 1; return -1; }
    if (sc == SC_ALT)                        {               return -1; }
    if (sc == SC_CAPS)                       { kb_caps ^= 1; return -1; }

    if (sc == SC_UP || sc == SC_DOWN || sc == SC_LEFT || sc == SC_RIGHT)
        return -(int)sc;
    if (sc >= SC_F1 && sc <= 0x44) return -1;
    if (sc >= 128) return -1;

    char c = kb_shift ? sc_shifted[sc] : sc_normal[sc];
    if (c == 0) return -1;

    if (kb_caps && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (kb_caps && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';

    if (kb_ctrl) {
        if (c >= 'a' && c <= 'z') return c - 'a' + 1;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
        return -1;
    }
    return (unsigned char)c;
}

static int kb_getchar(void)
{
    int c;
    while ((c = kb_poll_char()) == 0) ;
    return c;
}

/* ── tab completion ────────────────────────────────────────────────────── */

#define COMPLETE_MAX 64

static char tc_matches[COMPLETE_MAX][256];
static int  tc_is_dir[COMPLETE_MAX];
static int  tc_nmatch     = 0;
static int  tc_index      = -1;
static int  tc_word_start = 0;
static char tc_partial[256];
static char tc_dir_prefix[CWD_MAX * 2];

static void tab_reset(void)
{
    tc_nmatch = 0;
    tc_index  = -1;
}

static void tab_complete(char *buf, int *pos, int bufsz)
{
    buf[*pos] = '\0';

    if (tc_nmatch == 0) {
        int ws = *pos;
        while (ws > 0 && buf[ws-1] != ' ' && buf[ws-1] != '\t') ws--;
        tc_word_start = ws;
        const char *word = buf + ws;

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

        char abs_dir[CWD_MAX * 2];
        if (tc_dir_prefix[0] == '\0') {
            if (!getcwd(abs_dir, sizeof(abs_dir))) strcpy(abs_dir, "/");
        } else {
            resolve_path(tc_dir_prefix, abs_dir, sizeof(abs_dir));
        }

        DIR *d = opendir(abs_dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL && tc_nmatch < COMPLETE_MAX) {
                if (plen > 0 && strncmp(ent->d_name, tc_partial, plen) != 0)
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

            /* add ELFs from UTILS_PATH, stripping .elf suffix */
            DIR *ud = opendir(UTILS_PATH);
            if (ud) {
                struct dirent *ue;
                while ((ue = readdir(ud)) != NULL && tc_nmatch < COMPLETE_MAX) {
                    /* strip .elf to get the command name */
                    char uname[256];
                    strncpy(uname, ue->d_name, sizeof(uname) - 1);
                    uname[sizeof(uname) - 1] = '\0';
                    size_t ulen = strlen(uname);
                    if (ulen > 4 && strcmp(uname + ulen - 4, ".elf") == 0)
                        uname[ulen - 4] = '\0';

                    if (plen > 0 && strncmp(uname, tc_partial, plen) != 0)
                        continue;
                    int dup = 0;
                    for (int j = 0; j < tc_nmatch; j++)
                        if (strcmp(tc_matches[j], uname) == 0)
                            { dup = 1; break; }
                    if (!dup) {
                        strncpy(tc_matches[tc_nmatch], uname, 255);
                        tc_matches[tc_nmatch][255] = '\0';
                        tc_is_dir[tc_nmatch] = 0;
                        tc_nmatch++;
                    }
                }
                closedir(ud);
            }
        }

        if (tc_nmatch == 0) { putchar('\a'); fflush(stdout); return; }
        tc_index = -1;
    }

    tc_index = (tc_index + 1) % tc_nmatch;

    int erase = *pos - tc_word_start;
    for (int i = 0; i < erase; i++) fputs("\b \b", stdout);
    *pos = tc_word_start;

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

static int read_line(char *buf, int bufsz)
{
    int pos      = 0;
    int hist_idx = history_count;
    buf[0] = '\0';

    while (1) {
        int c = kb_getchar();

        if (c == 3) {                        /* Ctrl-C */
            tab_reset();
            fputs("^C\n", stdout); fflush(stdout);
            buf[0] = '\0';
            return 0;
        }
        if (c == 4) {                        /* Ctrl-D */
            tab_reset();
            putchar('\n');
            return -1;
        }
        if (c == 12) {                       /* Ctrl-L */
            tab_reset();
            fputs("\033[2J\033[H", stdout);
            print_prompt();
            fwrite(buf, 1, pos, stdout);
            fflush(stdout);
            continue;
        }

        if (c == -(int)SC_UP) {
            tab_reset();
            if (hist_idx > 0) {
                hist_idx--;
                for (int i = 0; i < pos; i++) fputs("\b \b", stdout);
                strncpy(buf, history[hist_idx], bufsz - 1);
                buf[bufsz-1] = '\0';
                pos = (int)strlen(buf);
                fputs(buf, stdout); fflush(stdout);
            }
            continue;
        }
        if (c == -(int)SC_DOWN) {
            tab_reset();
            for (int i = 0; i < pos; i++) fputs("\b \b", stdout);
            if (hist_idx < history_count - 1) {
                hist_idx++;
                strncpy(buf, history[hist_idx], bufsz - 1);
                buf[bufsz-1] = '\0';
            } else {
                hist_idx = history_count;
                buf[0] = '\0';
            }
            pos = (int)strlen(buf);
            fputs(buf, stdout); fflush(stdout);
            continue;
        }

        if (c < 0) continue;

        if (c == '\n' || c == '\r') {
            tab_reset();
            buf[pos] = '\0';
            putchar('\n'); fflush(stdout);
            return pos;
        }
        if (c == '\b') {
            tab_reset();
            if (pos > 0) { pos--; fputs("\b \b", stdout); fflush(stdout); }
            continue;
        }
        if (c == '\t') {
            buf[pos] = '\0';
            tab_complete(buf, &pos, bufsz);
            continue;
        }
        if (c >= 0x20 && pos < bufsz - 1) {
            tab_reset();
            buf[pos++] = (char)c;
            putchar(c); fflush(stdout);
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
        "  " OS_NAME " — Lua-native shell.  Type "
        COL_BOLD "help" COL_RESET " for commands.\n\n");
}

/* ── rc file ───────────────────────────────────────────────────────────── */

static void run_rc(void)
{
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

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    L = luaL_newstate();
    if (!L) { fputs("ssh: failed to create Lua state\n", stderr); return 1; }

    luaL_requiref(L, "_G",        luaopen_base,      1); lua_pop(L, 1);
    luaL_requiref(L, "math",      luaopen_math,      1); lua_pop(L, 1);
    luaL_requiref(L, "string",    luaopen_string,    1); lua_pop(L, 1);
    luaL_requiref(L, "table",     luaopen_table,     1); lua_pop(L, 1);
    luaL_requiref(L, "utf8",      luaopen_utf8,      1); lua_pop(L, 1);
    luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
    register_shell_lib(L);

    print_banner();

    if (chdir("/") < 0) chdir("/modules");

    run_rc();

    char line[MAX_LINE];
    while (running) {
        if (skip_prompt) { skip_prompt = 0; }
        else             { print_prompt(); }

        int n = read_line(line, sizeof(line));
        if (n < 0) { putchar('\n'); break; }
        if (n == 0) continue;
        if (line[0] == '#') continue;

        history_push(line);
        run_command(line);
    }

    lua_close(L);
    return 0;
}