# Entry 1 of epic-pass-and-play: SoloRounds (in the core library) now asks the Session's roster which seat to show
# (GameCore::seatShown), and Session no longer calls into Roster.cpp, so a suite whose own library builds Roster.cpp
# ahead of the core (match.cmake) no longer pulls it in. The roster joins the core library, where SoloRounds is.
list(APPEND HARNESS_EXTRA_CORE_SOURCES ${REPO_ROOT}/lib/GameCore/Roster.cpp)
