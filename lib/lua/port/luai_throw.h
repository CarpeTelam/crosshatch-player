/*
** Fork-owned (see ../README.md). library.json force-includes this header into
** every Lua unit (-I port, -include luai_throw.h), and test/game_script's CMake
** does the same, so src/ stays byte for byte. It defines Lua's throw and try
** macros before ldo.c's own (its #if !defined(LUAI_THROW) block then defines
** neither):
** LUAI_TRY is ldo.c's ISO C setjmp form, the one it picks without
** LUA_USE_POSIX, which no build here defines; LUAI_THROW is its longjmp form,
** which first calls luaport_memoryerror for a memory error.
**
** luaport_memoryerror runs at the throw, so it sees every memory error before
** anything can replace it while the stack unwinds (a __close that raises, in
** luaD_closeprotected). An allocation through Lua's own allocator (luaM) that
** the cap refuses is retried after an emergency collection, and throws only if
** that fails too, so a refusal it recovers from never reaches the hook. The
** library string buffers (lauxlib's resizebox: string.rep, table.concat,
** gsub, format, upper and lower past LUAL_BUFFERSIZE) call the allocator
** directly and throw at the first refusal, with no collection, so a game near
** its cap can end on a large string operation (as in stock Lua). The symbol is
** weak: lib/GameScript defines it (CallGuard.cpp), and a
** build that links Lua without it simply skips the call.
**
** Only macros and a declaration: this is read before lprefix.h, so it must not
** include a system header. ldo.c includes <setjmp.h> and lua.h itself.
*/
#ifndef luai_throw_h
#define luai_throw_h

struct lua_State;

extern void luaport_memoryerror(struct lua_State *L) __attribute__((weak));

#define LUAI_THROW(L,c) \
  (((c)->status == LUA_ERRMEM && luaport_memoryerror) ? luaport_memoryerror(L) \
                                                       : (void)0, \
   longjmp((c)->b, 1))

#define LUAI_TRY(L,c,f,ud)	if (setjmp((c)->b) == 0) ((f)(L, ud))

#endif
