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
// Where a text command's x is: its left edge, its middle, or its right edge.
enum class Align : uint8_t { Left, Center, Right };
// A frame's refresh request (ch.gfx.refresh), ordered so the larger one wins.
enum class Refresh : uint8_t { Fast, Half, Full };
enum class Op : uint8_t { Clear, Rect, Text, Line, Circle };

// A decoded command. Coordinates are canvas pixels; text points into the list and
// is NUL-terminated.
//   Clear   color
//   Rect    x, y, w, h, color, filled
//   Text    x, y, text, size, color, align
//   Line    x, y to x2, y2, color
//   Circle  centre x, y, radius r, color, filled
struct DrawCommand {
  Op op = Op::Clear;
  Color color = Color::White;
  bool filled = false;
  TextSize size = TextSize::Medium;
  Align align = Align::Left;
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  int16_t x2 = 0;
  int16_t y2 = 0;
  int16_t r = 0;
  const char* text = nullptr;
  uint16_t textLength = 0;
};

class DisplayList {
 public:
  DisplayList() = default;
  // capacity is capped at MAX_BYTES.
  DisplayList(uint8_t* storage, size_t capacity);

  // Empties the list and resets its refresh request to Fast.
  void clear();
  uint16_t count() const { return commands; }
  size_t bytes() const { return used; }
  // A 64-bit FNV-1a hash of the packed commands (not the refresh request), so
  // replay can tell a frame identical to the one on screen (AD-7).
  uint64_t hash() const;

  // Each returns false, appending nothing, when the command would pass a limit.
  // Coordinates outside int16_t are clamped. Colors are stored as given; the
  // bindings allow light and dark only for fills (AD-7).
  bool appendClear(Color color);
  bool appendRect(int64_t x, int64_t y, int64_t w, int64_t h, Color color, bool filled);
  bool appendText(int64_t x, int64_t y, const char* text, size_t length, TextSize size, Color color,
                  Align align = Align::Left);
  bool appendLine(int64_t x1, int64_t y1, int64_t x2, int64_t y2, Color color);
  bool appendCircle(int64_t x, int64_t y, int64_t r, Color color, bool filled);

  // The frame's refresh request: the largest one made since clear(). Not a
  // command, so it counts toward neither limit.
  void requestRefresh(Refresh mode) {
    if (mode > hint) hint = mode;
  }
  Refresh refresh() const { return hint; }

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
  Refresh hint = Refresh::Fast;
};

}  // namespace GameScript
