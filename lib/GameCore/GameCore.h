#pragma once

// GameCore: the game platform's domain (roster, session, turns, snapshots, wire
// protocol, manifests) and its ports. Host-testable C++ with no device, Lua, or
// src/ dependencies.
namespace GameCore {

// Returns "GameCore". Gives the skeleton a linkable symbol for the build and the
// host suite; it can go once the library has other source files.
const char* libraryName();

}  // namespace GameCore
