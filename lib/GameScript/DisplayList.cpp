#include "DisplayList.h"

#include <cstring>
#include <limits>

namespace GameScript {

// Layout, native byte order (writer and reader run on the same machine):
//   Clear  op u8, color u8
//   Rect   op u8, color u8, filled u8, x y w h i16
//   Text   op u8, color u8, size u8, x y i16, length u16, bytes, NUL
namespace {

constexpr size_t CLEAR_BYTES = 2;
constexpr size_t RECT_BYTES = 3 + 4 * sizeof(int16_t);
constexpr size_t TEXT_HEADER_BYTES = 3 + 2 * sizeof(int16_t) + sizeof(uint16_t);

int16_t clamp16(const int64_t v) {
  if (v < std::numeric_limits<int16_t>::min()) return std::numeric_limits<int16_t>::min();
  if (v > std::numeric_limits<int16_t>::max()) return std::numeric_limits<int16_t>::max();
  return static_cast<int16_t>(v);
}

uint8_t* put16(uint8_t* p, const int16_t v) {
  std::memcpy(p, &v, sizeof(v));
  return p + sizeof(v);
}

const uint8_t* get16(const uint8_t* p, int16_t& v) {
  std::memcpy(&v, p, sizeof(v));
  return p + sizeof(v);
}

}  // namespace

DisplayList::DisplayList(uint8_t* storage, const size_t capacity)
    : storage(storage), capacityBytes(capacity < MAX_BYTES ? capacity : MAX_BYTES) {}

void DisplayList::clear() {
  used = 0;
  commands = 0;
}

uint8_t* DisplayList::reserve(const size_t n) {
  if (commands >= MAX_COMMANDS || n > capacityBytes - used) return nullptr;
  uint8_t* p = storage + used;
  used += n;
  ++commands;
  return p;
}

bool DisplayList::appendClear(const Color color) {
  uint8_t* p = reserve(CLEAR_BYTES);
  if (!p) return false;
  p[0] = static_cast<uint8_t>(Op::Clear);
  p[1] = static_cast<uint8_t>(color);
  return true;
}

bool DisplayList::appendRect(const int64_t x, const int64_t y, const int64_t w, const int64_t h, const Color color,
                             const bool filled) {
  uint8_t* p = reserve(RECT_BYTES);
  if (!p) return false;
  *p++ = static_cast<uint8_t>(Op::Rect);
  *p++ = static_cast<uint8_t>(color);
  *p++ = filled ? 1 : 0;
  p = put16(p, clamp16(x));
  p = put16(p, clamp16(y));
  p = put16(p, clamp16(w));
  put16(p, clamp16(h));
  return true;
}

bool DisplayList::appendText(const int64_t x, const int64_t y, const char* text, const size_t length,
                             const TextSize size, const Color color) {
  if (length > std::numeric_limits<uint16_t>::max()) return false;
  uint8_t* p = reserve(TEXT_HEADER_BYTES + length + 1);
  if (!p) return false;
  *p++ = static_cast<uint8_t>(Op::Text);
  *p++ = static_cast<uint8_t>(color);
  *p++ = static_cast<uint8_t>(size);
  p = put16(p, clamp16(x));
  p = put16(p, clamp16(y));
  const auto len16 = static_cast<uint16_t>(length);
  std::memcpy(p, &len16, sizeof(len16));
  p += sizeof(len16);
  std::memcpy(p, text, length);
  p[length] = '\0';
  return true;
}

bool DisplayList::Reader::next(DrawCommand& out) {
  if (offset >= list.used) return false;
  const uint8_t* p = list.storage + offset;
  out = DrawCommand{};
  out.op = static_cast<Op>(*p++);
  out.color = static_cast<Color>(*p++);
  switch (out.op) {
    case Op::Clear:
      offset += CLEAR_BYTES;
      return true;
    case Op::Rect:
      out.filled = *p++ != 0;
      p = get16(p, out.x);
      p = get16(p, out.y);
      p = get16(p, out.w);
      get16(p, out.h);
      offset += RECT_BYTES;
      return true;
    case Op::Text: {
      out.size = static_cast<TextSize>(*p++);
      p = get16(p, out.x);
      p = get16(p, out.y);
      std::memcpy(&out.textLength, p, sizeof(out.textLength));
      p += sizeof(out.textLength);
      out.text = reinterpret_cast<const char*>(p);
      offset += TEXT_HEADER_BYTES + out.textLength + 1;
      return true;
    }
  }
  return false;
}

}  // namespace GameScript
