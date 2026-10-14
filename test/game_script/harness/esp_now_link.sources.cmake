# Entry 5 of epic-play-nearby: EspNowLink.cpp (the real ESP-NOW driver over WiFi, esp_now.h, and esp_wifi.h) and
# NearbyProbe.cpp (WiFi.getMode and the internal heap) are device code. Each is one whole-file #if on a device build,
# so the shared library leaves them out by name; the rest of the link (EspNowLink.h, FrameRing.h, RxGate.h,
# NearbyProbe.h) is header-only and esp_now_link.cmake's suite tests it. A match library that globs the same folder
# (match.cmake) starts from this list too.
list(APPEND HARNESS_EXCLUDE_GAMES EspNowLink.cpp NearbyProbe.cpp)
