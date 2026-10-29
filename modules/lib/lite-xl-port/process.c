#include "api/api.h"

/* Replaces lite-xl's src/api/process.c, which needs fork, pipes and threads. StrawOS has none
   of those yet, so the module keeps its constants and process.start() fails with a message;
   only plugins (linters, build runners and the like) start processes. */

enum
{
    STDIN_FD,
    STDOUT_FD,
    STDERR_FD,
    REDIRECT_DEFAULT = -1,
    REDIRECT_DISCARD = -2,
    REDIRECT_PARENT  = -3,
};

static int process_start(lua_State *L)
{
    lua_pushnil(L);
    lua_pushstring(L, "running other programs is not supported on StrawOS");
    lua_pushinteger(L, -1);
    return 3;
}

static int process_strerror(lua_State *L)
{
    (void)luaL_checkinteger(L, 1);
    lua_pushstring(L, "running other programs is not supported on StrawOS");
    return 1;
}

static const struct luaL_Reg lib[] = {
    { "start",    process_start    },
    { "strerror", process_strerror },
    { NULL, NULL }
};

int luaopen_process(lua_State *L)
{
    luaL_newlib(L, lib);

    API_CONSTANT_DEFINE(L, -1, "WAIT_INFINITE", -2);
    API_CONSTANT_DEFINE(L, -1, "WAIT_DEADLINE", -1);

    API_CONSTANT_DEFINE(L, -1, "STREAM_STDIN", STDIN_FD);
    API_CONSTANT_DEFINE(L, -1, "STREAM_STDOUT", STDOUT_FD);
    API_CONSTANT_DEFINE(L, -1, "STREAM_STDERR", STDERR_FD);

    API_CONSTANT_DEFINE(L, -1, "REDIRECT_DEFAULT", REDIRECT_DEFAULT);
    API_CONSTANT_DEFINE(L, -1, "REDIRECT_STDOUT", STDOUT_FD);
    API_CONSTANT_DEFINE(L, -1, "REDIRECT_STDERR", STDERR_FD);
    API_CONSTANT_DEFINE(L, -1, "REDIRECT_PARENT", REDIRECT_PARENT);
    API_CONSTANT_DEFINE(L, -1, "REDIRECT_DISCARD", REDIRECT_DISCARD);

    return 1;
}
