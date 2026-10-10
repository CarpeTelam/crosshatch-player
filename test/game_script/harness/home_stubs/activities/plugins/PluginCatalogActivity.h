#pragma once

// HomeActivity.cpp includes activities/plugins/PluginCatalogActivity.h for anyPluginInstalled(), which the real
// one answers by scanning the SD plugin folders (src/util/PluginLocations.h). The double answers from
// `pluginsInstalled()`, which a test sets.
inline bool& pluginsInstalled() {
  static bool installed = false;
  return installed;
}
inline bool anyPluginInstalled() { return pluginsInstalled(); }
