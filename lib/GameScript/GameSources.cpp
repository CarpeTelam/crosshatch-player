#include "GameSources.h"

#include <cstring>

namespace GameScript {

const SourceSpan* GameSources::find(const char* moduleName) const {
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(spans[i].name, moduleName) == 0) return &spans[i];
  }
  return nullptr;
}

}  // namespace GameScript
