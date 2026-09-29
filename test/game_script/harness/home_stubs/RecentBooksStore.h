#pragma once

#include <string>
#include <vector>

#include "HalStorage.h"

// RecentBook as src/RecentBooksStore.h has it, and the store as HomeActivity and CoverGridHomeUi call it. The
// real header includes ArduinoJson (which the host test job does not fetch) and the persistence base. A
// test fills `books`; a book whose file is not on the fake card is missing, as on the device.
struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooksStore {
 public:
  static RecentBooksStore& getInstance() {
    static RecentBooksStore instance;
    return instance;
  }
  const std::vector<RecentBook>& getBooks() const { return books; }
  void updateBook(const std::string& path, const std::string&, const std::string&, const std::string& coverBmpPath) {
    for (RecentBook& book : books)
      if (book.path == path) book.coverBmpPath = coverBmpPath;
  }
  static bool isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

  std::vector<RecentBook> books;
};

#define RECENT_BOOKS RecentBooksStore::getInstance()
