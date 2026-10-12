# Entry 8 of epic-play-nearby: what the shared libraries gain for GameLink and NearbySession.
#
# The seven lib/GameCore sources entries 6 and 7 added and no harness list had. (Session.cpp, Roster.cpp, and
# MatchLifecycle.cpp are listed already by match.cmake, pass_and_play.sources.cmake, and harness_base.sources.cmake;
# list nothing twice: a source in two libraries is two definitions.) Entries 9, 15, and 10 link game_match_src plus
# game_harness_core and get these seven from here.
list(APPEND HARNESS_EXTRA_CORE_SOURCES
     ${REPO_ROOT}/lib/GameCore/Protocol.cpp
     ${REPO_ROOT}/lib/GameCore/ReliableLink.cpp
     ${REPO_ROOT}/lib/GameCore/Lobby.cpp
     ${REPO_ROOT}/lib/GameCore/HostLobby.cpp
     ${REPO_ROOT}/lib/GameCore/GuestLobby.cpp
     ${REPO_ROOT}/lib/GameCore/StateQueue.cpp
     ${REPO_ROOT}/lib/GameCore/MatchSink.cpp)
# GameLink.cpp and NearbySession.cpp include freertos/task.h, which only the screen doubles provide, so they leave the
# shared library; nearby_session.cmake builds them against the screen doubles. (GameLinkStore.cpp needs only HalMemory
# and stays in the shared library.)
list(APPEND HARNESS_EXCLUDE_GAMES GameLink.cpp NearbySession.cpp)
