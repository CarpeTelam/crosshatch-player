#pragma once

#include <cstddef>
#include <cstdint>

namespace GameScript {

// A game's Lua sources as the match hands them to the VM (AD-5): one text blob and
// a table of module spans. The module name is the file name without ".lua"
// ("main" for main.lua), at most 32 characters.
struct SourceSpan {
  static constexpr size_t MAX_NAME_BYTES = 32;
  char name[MAX_NAME_BYTES + 1];
  uint32_t offset;
  uint32_t length;
};

struct GameSources {
  const SourceSpan* spans = nullptr;
  size_t count = 0;
  const char* text = nullptr;

  // Null when no module has this name.
  const SourceSpan* find(const char* moduleName) const;
  const char* textOf(const SourceSpan& span) const { return text + span.offset; }
};

}  // namespace GameScript
