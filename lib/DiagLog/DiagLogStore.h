#pragma once

#include "DiagLogWriter.h"

namespace DiagLog {

// The device's file store: the card through Storage / HalFile. Defined in src/diagnostics/DiagLogStorage.cpp, not in
// this library, because HalStorage.h needs SdFat's include path and PlatformIO's library finder does not carry it to a
// library in the cycle hal -> Logging -> DiagLog -> hal (naming SdFat here instead would add a node to the finder's
// graph in every build, define or not, and that moved the normal firmware by 112 bytes). A project source sees every
// library. Only built with CROSSHATCH_SD_LOG.
FileStore& deviceFileStore();

}  // namespace DiagLog
