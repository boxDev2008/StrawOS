#pragma once

/* Force-included into lite-xl's C sources, which were written against Lua 5.4.
   api/utf8.c is adapted from Lua's lutf8lib.c and uses lua_assert, which Lua 5.5 no longer
   exposes through its public headers. Release builds of Lua define it as a no-op too. */

#ifndef lua_assert
#define lua_assert(c) ((void)0)
#endif
