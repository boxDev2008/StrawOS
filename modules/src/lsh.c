#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <socket.h>
#include <poll.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <net.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define INIT_SCRIPT "/share/lsh/init.lua"
#define LINE_MAX    4096
#define INTERRUPT   0x03

static char   input_buf[LINE_MAX];
static size_t input_len;
static bool   input_eof;

static void input_take(size_t n)
{
    memmove(input_buf, input_buf + n, input_len - n);
    input_len -= n;
}

static bool input_fill(int timeout_ms)
{
    if (input_eof || input_len == sizeof(input_buf))
        return false;

    struct pollfd pfd = { 0, POLLIN, 0 };
    if (timeout_ms >= 0 && poll(&pfd, 1, timeout_ms) <= 0)
        return false;

    ssize_t n = read(0, input_buf + input_len, sizeof(input_buf) - input_len);
    if (n <= 0)
    {
        input_eof = true;
        return false;
    }
    input_len += (size_t)n;
    return true;
}

static bool input_take_interrupt(void)
{
    for (size_t i = 0; i < input_len; i++)
    {
        if (input_buf[i] == INTERRUPT)
        {
            input_take(i + 1);
            return true;
        }
    }
    return false;
}

static int read_line(char *out, size_t size)
{
    for (;;)
    {
        for (size_t i = 0; i < input_len; i++)
        {
            if (input_buf[i] == INTERRUPT)
            {
                input_take(i + 1);
                return -2;
            }
            if (input_buf[i] == '\n')
            {
                size_t n = i < size - 1 ? i : size - 1;
                memcpy(out, input_buf, n);
                out[n] = '\0';
                input_take(i + 1);
                return (int)n;
            }
        }

        if (input_len == sizeof(input_buf))
        {
            size_t n = size - 1 < input_len ? size - 1 : input_len;
            memcpy(out, input_buf, n);
            out[n] = '\0';
            input_take(input_len);
            return (int)n;
        }

        if (!input_fill(-1))
        {
            if (input_len == 0)
                return -1;
            size_t n = input_len < size - 1 ? input_len : size - 1;
            memcpy(out, input_buf, n);
            out[n] = '\0';
            input_take(input_len);
            return (int)n;
        }
    }
}

static void interrupt_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    input_fill(0);
    if (input_take_interrupt())
        luaL_error(L, "interrupted");
}

static const char *type_name(enum stat_type t)
{
    switch (t)
    {
        case STAT_DIR:    return "dir";
        case STAT_DEVICE: return "device";
        case STAT_SOCKET: return "socket";
        default:          return "file";
    }
}

static int l_list(lua_State *L)
{
    const char *path = luaL_optstring(L, 1, ".");
    char full[256];
    if (!realpath(path, full))
        return luaL_fileresult(L, 0, path);

    int fd = open(full, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || st.type != STAT_DIR)
    {
        if (fd >= 0)
            close(fd);
        lua_pushnil(L);
        lua_pushfstring(L, "%s: not a directory", path);
        return 2;
    }

    lua_newtable(L);
    struct dirent de;
    int i = 1;
    while (readdir(fd, &de) == 1)
    {
        char child[320];
        snprintf(child, sizeof(child), "%s/%s", strcmp(full, "/") == 0 ? "" : full, de.name);
        struct stat cst;
        bool ok = stat(child, &cst) == 0;

        lua_newtable(L);
        lua_pushstring(L, de.name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, type_name(ok ? cst.type : de.type));
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ok ? (lua_Integer)cst.size : 0);
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, i++);
    }
    close(fd);
    return 1;
}

static int l_stat(lua_State *L)
{
    struct stat st;
    if (stat(luaL_checkstring(L, 1), &st) != 0)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, type_name(st.type));
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, (lua_Integer)st.size);
    lua_setfield(L, -2, "size");
    return 1;
}

static int l_cwd(lua_State *L)
{
    char buf[256];
    lua_pushstring(L, getcwd(buf, sizeof(buf)) ? buf : "/");
    return 1;
}

static int l_cd(lua_State *L)
{
    const char *path = luaL_optstring(L, 1, "/");
    if (chdir(path) != 0)
    {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: no such directory", path);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_realpath(lua_State *L)
{
    char buf[256];
    if (!realpath(luaL_checkstring(L, 1), buf))
        return 0;
    lua_pushstring(L, buf);
    return 1;
}

static int l_mkdir(lua_State *L)
{
    return luaL_fileresult(L, mkdir(luaL_checkstring(L, 1), 0) == 0, lua_tostring(L, 1));
}

static int l_which(lua_State *L)
{
    char buf[256];
    if (!find_program(luaL_checkstring(L, 1), buf))
        return 0;
    lua_pushstring(L, buf);
    return 1;
}

static int l_spawn(lua_State *L)
{
    int n = lua_gettop(L);
    const char *path = luaL_checkstring(L, 1);
    const char *argv[32];
    int argc = 0;
    argv[argc++] = path;
    for (int i = 2; i <= n && argc < 31; i++)
        argv[argc++] = luaL_checkstring(L, i);
    argv[argc] = NULL;
    pid_t pid = spawnp(path, argv);
    if (!pid)
        return 0;
    lua_pushinteger(L, (lua_Integer)pid);
    return 1;
}

static int l_run(lua_State *L)
{
    if (!l_spawn(L))
    {
        lua_pushnil(L);
        lua_pushstring(L, "could not start the program");
        return 2;
    }
    pid_t pid = (pid_t)lua_tointeger(L, -1);
    lua_pop(L, 1);

    fflush(stdout);
    for (;;)
    {
        int code = 0;
        int st = task_status(pid, &code);
        if (st == TASK_EXITED)
        {
            lua_pushinteger(L, code);
            return 1;
        }
        if (st == TASK_DETACHED)
        {
            lua_pushinteger(L, 0);
            lua_pushboolean(L, 1);
            return 2;
        }
        input_fill(20);
        if (input_take_interrupt())
        {
            kill(pid);
            return luaL_error(L, "interrupted");
        }
    }
}

static int l_exec(lua_State *L)
{
    const char *command = luaL_checkstring(L, 1);
    fflush(stdout);
    int code = system(command);
    if (code < 0)
    {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: could not start the program", command);
        return 2;
    }
    lua_pushinteger(L, code);
    return 1;
}

static int l_kill(lua_State *L)
{
    lua_pushboolean(L, kill((pid_t)luaL_checkinteger(L, 1)) == 0);
    return 1;
}

static int l_sleep(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    uint64_t end = uptime_ms() + (uint64_t)(ms > 0 ? ms : 0);
    while (uptime_ms() < end)
    {
        uint64_t left = end - uptime_ms();
        wait(left > 50 ? 50 : (uint32_t)left);
        interrupt_hook(L, NULL);
    }
    return 0;
}

static int l_uptime(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)uptime_ms());
    return 1;
}

static int l_mem(lua_State *L)
{
    struct meminfo m;
    if (meminfo(&m) != 0)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)(m.total_pages * m.page_size));
    lua_setfield(L, -2, "total");
    lua_pushinteger(L, (lua_Integer)(m.free_pages * m.page_size));
    lua_setfield(L, -2, "free");
    return 1;
}

static int l_readline(lua_State *L)
{
    static char line[LINE_MAX];
    int n = read_line(line, sizeof(line));
    if (n == -1)
        return 0;
    if (n == -2)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushlstring(L, line, (size_t)n);
    return 1;
}

static void push_ip(lua_State *L, const uint8_t ip[4])
{
    lua_pushfstring(L, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

static int l_resolve(lua_State *L)
{
    uint8_t ip[4];
    if (net_resolve(luaL_checkstring(L, 1), ip) != 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, "could not resolve host");
        return 2;
    }
    push_ip(L, ip);
    return 1;
}

static int l_netinfo(lua_State *L)
{
    struct net_info info;
    if (net_info(&info) != 0)
        return 0;
    lua_newtable(L);
    lua_pushboolean(L, info.up);
    lua_setfield(L, -2, "up");
    push_ip(L, info.ip);
    lua_setfield(L, -2, "ip");
    push_ip(L, info.gateway);
    lua_setfield(L, -2, "gateway");
    push_ip(L, info.netmask);
    lua_setfield(L, -2, "netmask");
    push_ip(L, info.dns);
    lua_setfield(L, -2, "dns");
    return 1;
}

static int l_connect(lua_State *L)
{
    int fd = net_connect(luaL_checkstring(L, 1), (uint16_t)luaL_checkinteger(L, 2));
    if (fd < 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, fd == -ETIMEDOUT ? "connection timed out" : fd == -ECONNREFUSED ? "connection refused" :
                          fd == -ENETDOWN ? "network is down" : "could not connect");
        return 2;
    }
    lua_pushinteger(L, fd);
    return 1;
}

static int l_send(lua_State *L)
{
    size_t len;
    int fd = (int)luaL_checkinteger(L, 1);
    const char *data = luaL_checklstring(L, 2, &len);
    size_t done = 0;
    while (done < len)
    {
        ssize_t n = write(fd, data + done, len - done);
        if (n <= 0)
            return luaL_error(L, "send failed");
        done += (size_t)n;
    }
    return 0;
}

static int l_recv(lua_State *L)
{
    int fd = (int)luaL_checkinteger(L, 1);
    lua_Integer max = luaL_optinteger(L, 2, 4096);
    lua_Integer timeout = luaL_optinteger(L, 3, -1);
    if (max <= 0 || max > 65536)
        max = 4096;

    uint64_t start = uptime_ms();
    for (;;)
    {
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 50) > 0)
            break;
        interrupt_hook(L, NULL);
        if (timeout >= 0 && uptime_ms() - start >= (uint64_t)timeout)
            return 0;
    }

    char *buf = malloc((size_t)max);
    ssize_t n = read(fd, buf, (size_t)max);
    if (n > 0)
        lua_pushlstring(L, buf, (size_t)n);
    free(buf);
    return n > 0 ? 1 : 0;
}

static int l_close(lua_State *L)
{
    close((int)luaL_checkinteger(L, 1));
    return 0;
}

static const luaL_Reg system_lib[] = {
    { "exec",     l_exec },
    { "resolve",  l_resolve },
    { "netinfo",  l_netinfo },
    { "connect",  l_connect },
    { "send",     l_send },
    { "recv",     l_recv },
    { "close",    l_close },
    { "list",     l_list },
    { "stat",     l_stat },
    { "cwd",      l_cwd },
    { "cd",       l_cd },
    { "realpath", l_realpath },
    { "mkdir",    l_mkdir },
    { "sleep",    l_sleep },
    { "uptime",   l_uptime },
    { "mem",      l_mem },
    { "readline", l_readline },
    { NULL, NULL },
};

static const luaL_Reg shell_lib[] = {
    { "which",    l_which },
    { "spawn",    l_spawn },
    { "run",      l_run },
    { "kill",     l_kill },
    { NULL, NULL },
};

static int connect_terminal(const char *name)
{
    int fd = socket(0);
    if (fd < 0)
        return -1;
    for (int i = 0; i < 50; i++)
    {
        if (connect(fd, name) == 0)
        {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            return 0;
        }
        wait(20);
    }
    close(fd);
    return -1;
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "--terminal") == 0 && connect_terminal(argv[2]) != 0)
        return 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    lua_State *L = luaL_newstate();
    if (!L)
    {
        fprintf(stderr, "lsh: not enough memory for Lua\n");
        return 1;
    }
    luaL_openlibs(L);

    luaL_newlib(L, system_lib);
    lua_setglobal(L, "system");
    luaL_newlib(L, shell_lib);
    lua_setglobal(L, "shell");

    lua_sethook(L, interrupt_hook, LUA_MASKCOUNT, 20000);

    if (luaL_dofile(L, INIT_SCRIPT) != LUA_OK)
    {
        fprintf(stderr, "lsh: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    lua_getglobal(L, "shell");
    int next = 1;
    if (argc > 3 && strcmp(argv[1], "--terminal") == 0)
    {
        lua_pushinteger(L, atoi(argv[3]));
        lua_setfield(L, -2, "columns");
        next = 4;
    }
    if (next + 1 < argc && strcmp(argv[next], "--run") == 0)
    {
        lua_pushstring(L, argv[next + 1]);
        lua_setfield(L, -2, "run_script");
        lua_newtable(L);
        for (int i = next + 2; i < argc; i++)
        {
            lua_pushstring(L, argv[i]);
            lua_rawseti(L, -2, i - next - 1);
        }
        lua_setfield(L, -2, "run_args");
    }
    lua_getfield(L, -1, "main");
    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
        fprintf(stderr, "lsh: %s\n", lua_tostring(L, -1));
    int code = (int)lua_tointeger(L, -1);

    fflush(stdout);
    lua_close(L);
    return code;
}
