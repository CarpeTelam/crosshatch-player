# Lua 5.5.1 (vendored)

`src/` is the `src/` directory of the official PUC Lua 5.5.1 release, byte for byte, including its `Makefile`.
Nothing in it is edited. Everything else in this directory belongs to the fork.

- Source: <https://www.lua.org/ftp/lua-5.5.1.tar.gz>
- SHA-256 of the tarball: `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce`, equal to the checksum
  lua.org lists for it on <https://www.lua.org/ftp/> (checked 2026-09-26).
- License: MIT, stated at the end of `src/lua.h`.

## Fork-owned files

- `library.json` -- builds `src/` as C. Its `srcFilter` leaves out the stand-alone interpreter and compiler
  (`lua.c`, `luac.c`), the default library loader (`linit.c`), and the libraries a sandboxed game must not reach
  (`liolib.c`, `loslib.c`, `ldblib.c`, `loadlib.c`, `lcorolib.c`). Its one flag, `-DLUA_COMPAT_GLOBAL=0`, turns off
  the only compatibility option 5.5.1 enables by default, so `global` stays a reserved word. The host tests in
  `test/game_script` read the exclusions and the flag from this file and refuse any entry they cannot mirror, so the
  host build follows the manifest; that PlatformIO applies it shows in each firmware build log (26 `lib/lua` units)
  and compile database (`-DLUA_COMPAT_GLOBAL=0` on those units). Leaving a library out of the build is only the
  first layer: the host that creates a state still chooses which libraries to open and which base functions to keep.
- `port/luai_throw.h` -- force-included into every Lua unit by `library.json`'s `-I port` and
  `-include luai_throw.h` (and by `test/game_script`, which mirrors both). It defines `LUAI_TRY` and `LUAI_THROW` as
  `ldo.c` would here (ISO C `setjmp`/`longjmp`), except that `LUAI_THROW` first calls the weak
  `luaport_memoryerror(L)` for a memory error; `lib/GameScript/CallGuard.cpp` defines it to record the heap cap as a
  sticky fault (spine AD-4, AD-6).
- `.clang-format` -- `DisableFormat: true` keeps the whole-tree format check from rewriting `src/`.
- `README.md` -- this note.

C++ code includes `<climits>` and then `<lua.hpp>` (Lua's own `extern "C"` wrapper), and asserts
`sizeof(lua_Integer) == 8`: games rely on 64-bit integers.

## Checking or upgrading

```sh
curl -LO https://www.lua.org/ftp/lua-5.5.1.tar.gz
sha256sum lua-5.5.1.tar.gz          # compare with the value above and with https://www.lua.org/ftp/
tar xzf lua-5.5.1.tar.gz
diff -r lua-5.5.1/src lib/lua/src   # prints nothing
```

To upgrade, replace `src/` with the new release's `src/` and update the version and checksum here and in
`library.json`. Then check the new `luaconf.h` for compatibility options that default to on, check for new `.c` files
(`+<*>` compiles every one not excluded), update the version check in `test/game_script/LuaOnHostTest.cpp`, and run
the host tests.
