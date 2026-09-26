#pragma once

// GameScript: the Lua script adapter (VM host, sandbox, ch.* bindings, codec,
// frame buffers). Depends on GameCore, GameIcons, and lib/lua only; platform
// services reach it through injected ports.
namespace GameScript {

// Returns "GameScript". Gives the skeleton a linkable symbol for the build and the
// host suite; it can go once the library has other source files.
const char* libraryName();

}  // namespace GameScript
