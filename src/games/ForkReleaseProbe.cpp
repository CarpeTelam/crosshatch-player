#if FREEINK_CAP_GAMES

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
  // Same transport settings as HttpDownloader's release fetch. Without wolfSSL
  // the https request fails at connect and the probe reports false, which
  // leaves the caller's error as it was.
  http->setInsecure();
  http->setReuse(false);
  http->setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (!http->begin(ForkRelease::LATEST_RELEASE_URL)) return false;

  const int status = http->GET([](const uint8_t*, size_t) { return true; });
  LOG_DBG("OTA", "Release probe status: %d", status);
  return status == HTTP_NOT_FOUND;
}

#endif  // FREEINK_CAP_GAMES
