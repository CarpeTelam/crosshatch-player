#pragma once

// resume.bin's bytes and the two package hashes the save suites use (GameSaveStoreTest, ResumeMatchTest,
// ModePickerTest). The file is written out by hand here, never through GameSaveStore, so a suite compares what the
// store writes against the layout in docs/crosshatch/formats.md and not against the store's own idea of it.

#include <cstdint>
#include <vector>

#include "GameSaveStore.h"

namespace savefile {

using Bytes = std::vector<uint8_t>;

// The .pkg text "v1\n0530a15766e91bf1\n" holds HASH_A; HASH_B differs in the last byte.
inline constexpr uint8_t HASH_A[GameSaveStore::PACKAGE_HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1};
inline constexpr uint8_t HASH_B[GameSaveStore::PACKAGE_HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf2};

// {taps = n}: the codec bytes the counting game's snapshot has.
inline Bytes snapshotOf(const uint8_t taps) {
  return Bytes{0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, static_cast<uint8_t>(taps << 1)};
}

// resume.bin's bytes: the blob header (magic, file version, codec version), the package hash, mode, seat count, ver
// (u16 LE), the snapshot.
inline Bytes resumeBytes(const Bytes& snapshot, const uint16_t ver = 7,
                         const uint8_t (&hash)[GameSaveStore::PACKAGE_HASH_BYTES] = HASH_A, const uint8_t mode = 0,
                         const uint8_t seats = 1, const char* magic = "CHRS", const uint8_t fileVersion = 1,
                         const uint8_t codecVersion = 1) {
  Bytes out = {static_cast<uint8_t>(magic[0]),
               static_cast<uint8_t>(magic[1]),
               static_cast<uint8_t>(magic[2]),
               static_cast<uint8_t>(magic[3]),
               fileVersion,
               codecVersion};
  out.insert(out.end(), hash, hash + GameSaveStore::PACKAGE_HASH_BYTES);
  out.push_back(mode);
  out.push_back(seats);
  out.push_back(static_cast<uint8_t>(ver & 0xFF));
  out.push_back(static_cast<uint8_t>(ver >> 8));
  out.insert(out.end(), snapshot.begin(), snapshot.end());
  return out;
}

}  // namespace savefile
