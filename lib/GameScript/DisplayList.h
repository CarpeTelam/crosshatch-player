#pragma once

#include <cstddef>
#include <cstdint>

namespace GameScript {

// One frame's drawing, as packed commands in caller-owned storage (PSRAM on the
// device). ch.gfx appends on the VM task; FrameReplay reads on the render task.
// Limits per frame (AD-7): at most MAX_COMMANDS commands and MAX_BYTES bytes.
inline constexpr uint16_t MAX_COMMANDS = 2048;
inline constexpr size_t MAX_BYTES = 32 * 1024;

enum class Color : uint8_t { White, Light, Dark, Black };
enum class TextSize : uint8_t { Small, Medium, Large };
enum class Op : uint8_t { Clear, Rect, Text };

// A decoded command. Coordinates are canvas pixels; text points into the list and
// is NUL-terminated.
struct DrawCommand {
  Op op = Op::Clear;
  Color color = Color::White;
  bool filled = false;
  TextSize size = TextSize::Medium;
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  const char* text = nullptr;
  uint16_t textLength = 0;
};

class DisplayList {
 public:
  DisplayList() = default;
  // capacity is capped at MAX_BYTES.
  DisplayList(uint8_t* storage, size_t capacity);

  void clear();
  uint16_t count() const { return commands; }
  size_t bytes() const { return used; }

  // Each returns false, appending nothing, when the command would pass a limit.
  // Coordinates outside int16_t are clamped.
  bool appendClear(Color color);
  bool appendRect(int64_t x, int64_t y, int64_t w, int64_t h, Color color, bool filled);
  bool appendText(int64_t x, int64_t y, const char* text, size_t length, TextSize size, Color color);

  class Reader {
   public:
    explicit Reader(const DisplayList& list) : list(list) {}
    // Decodes the next command; false at the end.
    bool next(DrawCommand& out);

   private:
    const DisplayList& list;
    size_t offset = 0;
  };
  Reader reader() const { return Reader(*this); }

 private:
  // Reserves n bytes for one command; null when a limit would be passed.
  uint8_t* reserve(size_t n);

  uint8_t* storage = nullptr;
  size_t capacityBytes = 0;
  size_t used = 0;
  uint16_t commands = 0;
};

}  // namespace GameScript
