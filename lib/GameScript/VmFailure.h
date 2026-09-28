#pragma once

#include <cstdint>

#include "LuaGame.h"

namespace GameScript {

// Why a GameVM failed (AD-14), kept pure so the error view's wording is host-tested.
// Script: the script's own error, worded by Lua's message. NoSession: the arena's
// reserve had no room for the Session, before any Lua ran (the game could not
// start). OutOfMemory: LuaGame::load's scratch or Lua state did not fit.
// NotLoaded: a call into the game came before its load; defensive, since the VM
// calls an entry only after load() returned Ok.
enum class VmFailure : uint8_t { None, Script, NoSession, OutOfMemory, NotLoaded };

// The failure of a VM that `failed` (ended with a ScriptError): None when it did
// not; NoSession when the Session never fit (`sessionOutOfMemory`), whatever LuaGame
// says; else LuaGame's host failure `host`; else Script.
VmFailure vmFailure(bool failed, bool sessionOutOfMemory, LuaGame::HostFailure host);

// True when the failure came before any game code ran, so the error view's
// headline says the game could not start: NoSession only.
bool failedToStart(VmFailure failure);

// The host's own words for the failures it words itself, tr() text from the
// caller (lib/GameScript may not include lib/I18n).
struct HostFailureTexts {
  const char* outOfMemory = "";
  const char* notLoaded = "";
};

// The error view's detail: the host's text for NoSession and OutOfMemory
// (outOfMemory) and NotLoaded (notLoaded); `scriptMessage` (Lua's message) for
// Script and None.
const char* failureDetail(VmFailure failure, const HostFailureTexts& texts, const char* scriptMessage);

}  // namespace GameScript
