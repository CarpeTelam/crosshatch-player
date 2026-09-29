# Entry 3's part of the shared source set: the SHA-256 helper compiles here against
# OpenSSL (mbedTLS is a firmware library), and a missing OpenSSL fails the configure
# instead of skipping the hash test. GamePackageInstaller is built in installer.cmake, with
# the converter's stubs (a panel size, vTaskDelay), so the shared library leaves it out.
find_package(OpenSSL REQUIRED)
list(APPEND HARNESS_EXCLUDE_GAMES GamePackageInstaller.cpp)
list(APPEND HARNESS_EXTRA_DEFINES GAME_HASH_OPENSSL=1)
list(APPEND HARNESS_EXTRA_LIBS OpenSSL::Crypto)
