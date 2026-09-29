#pragma once

// The one question HomeActivity asks of the OPDS server list (src/OpdsServerStore.h): whether there is a
// server, which adds the OPDS row and tab. A test sets `servers`.
class OpdsServerStore {
 public:
  static OpdsServerStore& getInstance() {
    static OpdsServerStore instance;
    return instance;
  }
  bool hasServers() const { return servers; }
  bool servers = false;
};

#define OPDS_STORE OpdsServerStore::getInstance()
