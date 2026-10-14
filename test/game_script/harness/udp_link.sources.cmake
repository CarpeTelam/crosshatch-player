# Entry 16 of epic-play-nearby: UdpLink.cpp (the simulator's ILink over UDP on loopback) is one whole-file #if on
# FREEINK_CAP_GAMES && SIMULATOR, so in the shared libraries (SIMULATOR undefined) it would compile to nothing. The
# shared and match libraries leave it out by name, and udp_link.cmake builds it into its own library with SIMULATOR on.
list(APPEND HARNESS_EXCLUDE_GAMES UdpLink.cpp)
