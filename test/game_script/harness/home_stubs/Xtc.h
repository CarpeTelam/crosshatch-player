#pragma once

#include <string>
#include <utility>

// Xtc as HomeActivity uses it (lib/Xtc/Xtc.h): see Epub.h in this folder.
class Xtc {
 public:
  explicit Xtc(std::string filepath, const std::string&) : filepath(std::move(filepath)) {}
  bool load() { return true; }
  std::string getThumbBmpPath() const { return filepath + ".thumb.bmp"; }
  std::string getThumbBmpPath(int height) const { return filepath + ".thumb_" + std::to_string(height) + ".bmp"; }
  bool generateThumbBmp(int) const { return false; }

 private:
  std::string filepath;
};
