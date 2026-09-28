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
** luaD_closeprotected). Lua throws one only after its emergency collection
** failed to make room, so a refused allocation Lua recovers from never reaches
** it. The symbol is weak: lib/GameScript defines it (CallGuard.cpp), and a
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
