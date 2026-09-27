// Device-only: the simulator's SecureHttpClient has no setUserAgent or streaming GET, and the one caller,
// network/OtaUpdater.cpp, is not built there.
#if FREEINK_CAP_GAMES && !defined(SIMULATOR)

#include "ForkReleaseProbe.h"

#include <ForkRelease.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>

namespace {
constexpr int HTTP_NOT_FOUND = 404;
}  // namespace

bool ForkReleaseProbe::latestReleaseMissing() {
  // Heap, not stack: the client holds two network clients and its buffers, well
  // over the stack budget for one frame. Freed on return.
  auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
  if (!http) {
    LOG_ERR("OTA", "OOM: %u byte release probe client", static_cast<unsigned>(sizeof(freeink::SecureHttpClient)));
    return false;
  }
  // TLS mode and user agent as HttpDownloader's release fetch; the timeout
  // (15 s) and redirect limit (none: a 3xx is not 404, so false) are
  // SecureHttpClient's defaults, not HttpDownloader's 60 s and 5 hops.
  // docs/crosshatch/upstream-touches.md records the four values beside ledger
  // row 10. Without wolfSSL the https request fails at connect and the probe
  // reports false, which leaves the caller's error as it was.
  http->setInsecure();
  http->setReuse(false);
  http->setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (!http->begin(ForkRelease::LATEST_RELEASE_URL)) return false;

  const int status = http->GET([](const uint8_t*, size_t) { return true; });
  LOG_DBG("OTA", "Release probe status: %d", status);
  return status == HTTP_NOT_FOUND;
}

#endif  // FREEINK_CAP_GAMES && !defined(SIMULATOR)
