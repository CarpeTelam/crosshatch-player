#include "VmFailure.h"

namespace GameScript {

VmFailure vmFailure(const bool failed, const bool sessionOutOfMemory, const LuaGame::HostFailure host) {
  if (!failed) return VmFailure::None;
  if (sessionOutOfMemory) return VmFailure::NoSession;
  switch (host) {
    case LuaGame::HostFailure::OutOfMemory:
      return VmFailure::OutOfMemory;
    case LuaGame::HostFailure::NotLoaded:
      return VmFailure::NotLoaded;
    case LuaGame::HostFailure::None:
      break;
  }
  return VmFailure::Script;
}

bool failedToStart(const VmFailure failure) {
  switch (failure) {
    case VmFailure::NoSession:
    case VmFailure::OutOfMemory:
    case VmFailure::NotLoaded:
      return true;  // before any game code ran
    case VmFailure::Script:
    case VmFailure::None:
      break;
  }
  return false;
}

const char* failureDetail(const VmFailure failure, const HostFailureTexts& texts, const char* const scriptMessage) {
  switch (failure) {
    case VmFailure::NoSession:
    case VmFailure::OutOfMemory:
      return texts.outOfMemory;
    case VmFailure::NotLoaded:
      return texts.notLoaded;
    case VmFailure::Script:
    case VmFailure::None:
      break;
  }
  return scriptMessage;
}

}  // namespace GameScript
