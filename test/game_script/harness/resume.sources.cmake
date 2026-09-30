# Entry 11's part of the shared source set: GameMatchActivity reads the installed package's
# hash with GameRegistry::readPackageHash, so linking the match now pulls in GameRegistry.o,
# which reads manifests. The manifest parser and its JSON reader join the core library so
# GameMatchHarnessTest (match.cmake) still links.
list(APPEND HARNESS_EXTRA_CORE_SOURCES
     ${REPO_ROOT}/lib/GameCore/Manifest.cpp
     ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp)
