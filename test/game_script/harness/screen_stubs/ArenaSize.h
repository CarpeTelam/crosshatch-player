#pragma once

// The size of the reserve GameArena carves out of the VM's block for the Session and the
// codec scratch, which a test shrinks to make a VM run out of memory before any game code
// runs (NoSession: the Session does not fit; OutOfMemory: the scratch does not). The
// default is the firmware's. Set it before GameVM::create, and put it back after.

#include <ArenaAllocator.h>

namespace fakearena {

inline size_t reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;

}  // namespace fakearena
