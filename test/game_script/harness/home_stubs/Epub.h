#pragma once

#include <string>
#include <utility>

// Epub as HomeActivity uses it (lib/Epub/Epub.h): the cover thumbnail paths and their generation. The real class
// parses the book. Here a thumbnail is a file the test may have put on the fake card, and generating
// one succeeds or fails as `Epub::generates` says; `Epub::generated` counts the calls.
class Epub {
 public:
  explicit Epub(std::string filepath, const std::string&) : filepath(std::move(filepath)) {}
  bool load(bool = true, bool = false) { return true; }
  std::string getThumbBmpPath() const { return filepath + ".thumb.bmp"; }
  std::string getThumbBmpPath(int height) const { return filepath + ".thumb_" + std::to_string(height) + ".bmp"; }
  bool generateThumbBmp(int) const {
    ++generated();
    return generates();
  }
  bool generateThumbBmpFromSource(int) {
    ++generated();
    return generates();
  }

  static int& generated() {
    static int count = 0;
    return count;
  }
  static bool& generates() {
    static bool result = false;
    return result;
  }

 private:
  std::string filepath;
};
