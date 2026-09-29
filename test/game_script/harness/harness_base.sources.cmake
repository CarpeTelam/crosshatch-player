# Entry 1's part of the shared source set: what the harness leaves out of
# game_harness_src, and why. An entry comes off this list when the suite that needs the
# file builds it (with the double it wants, in its own library). A new firmware-only
# source in src/games is excluded in the file of the entry that adds it.
list(APPEND HARNESS_EXCLUDE_GAMES
     # Arduino millis and esp_timer / esp_random: firmware clocks and entropy.
     GameClock.cpp
     GameRandom.cpp
     # FreeRTOS tasks and Arduino.h (the VM task and its watchdog).
     GameVM.cpp
     # The ESP-IDF HTTPS client and mbedtls.
     ForkReleaseProbe.cpp
     # Links every firmware component so the linker keeps them; it has no behaviour of its own.
     GamesBuildAnchor.cpp)
list(APPEND HARNESS_EXCLUDE_ACTIVITIES
     # The activity framework, UITheme, and the SDK's UI: the match and list screens on
     # the host are the next harness entries.
     GameMatchActivity.cpp
     GamesLauncherActivity.cpp)
