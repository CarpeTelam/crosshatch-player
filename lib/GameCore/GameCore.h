#pragma once

// GameCore: the game platform's domain (roster, session, turns, snapshots, wire
// protocol, manifests) and its ports. Host-testable C++ with no device, Lua, or
// src/ dependencies. Empty until the game epics add units.
namespace GameCore {

// Returns "GameCore". Gives the skeleton a linkable symbol for the build and the
// host suite; later stories may remove it.
const char* libraryName();

}  // namespace GameCore
