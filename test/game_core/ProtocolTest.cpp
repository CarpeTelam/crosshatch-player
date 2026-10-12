#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "Protocol.h"

// GameCore::Protocol: the nearby-play frame codec (AD-13). The golden vectors are written by hand from the byte
// tables in Protocol.h, so they are an oracle independent of encode.

using namespace GameCore;
using namespace GameCore::Protocol;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t SESSION = 0x11223344;
constexpr size_t SESSION_AT = 4;
constexpr size_t PAYLOAD_AT = HEADER_BYTES;

static_assert(MAX_FRAME_BYTES == 1470);
static_assert(MAX_STATE_FRAME_BYTES == 1418);
static_assert(MAX_STATE_FRAME_BYTES <= MAX_FRAME_BYTES);
static_assert(MAX_SNAPSHOT_BYTES == 1400);
static_assert(HEADER_BYTES == 12);
static_assert(PROTO == 1);

// Storage the sample frames' spans point into.
const Bytes LABEL_X4 = {0x58, 0x34, 0x20, 0x33, 0x46, 0x32, 0x41};  // "X4 3F2A"
const Bytes SNAP_TEXT = {0x73, 0x6e, 0x61, 0x70};                   // "snap"
const Bytes SNAP_ONE = {0xff};

std::array<uint8_t, PKG_HASH_BYTES> hashFrom(const uint8_t first) {
  std::array<uint8_t, PKG_HASH_BYTES> hash{};
  for (size_t i = 0; i < hash.size(); i++) hash[i] = static_cast<uint8_t>(first + i);
  return hash;
}

Header headerOf(const Type type, const uint16_t seq = 0, const uint16_t ack = 0, const uint32_t session = SESSION) {
  Header h;
  h.type = type;
  h.session = session;
  h.seq = seq;
  h.ack = ack;
  return h;
}

Frame advertFrame() {
  Frame f;
  f.header = headerOf(Type::Advert);
  f.advert.gameId = "tic";
  f.advert.pkgHash = hashFrom(0xa0);
  f.advert.manifestApi = 1;
  f.advert.hostApi = 1;
  f.advert.surface = 0xEE049B84;
  f.advert.seatsMax = 2;
  f.advert.seatsTaken = 1;
  f.advert.hostLabel = LABEL_X4;
  return f;
}

Frame joinFrame() {
  Frame f;
  f.header = headerOf(Type::Join, 5, 6);
  f.join.pkgHash = hashFrom(0xb0);
  f.join.hostApi = 2;
  f.join.surface = 0x01020304;
  return f;
}

Frame acceptFrame() {
  Frame f;
  f.header = headerOf(Type::Accept, 0x0102, 0x0304);
  f.accept.seat = 2;
  f.accept.n = 2;
  return f;
}

Frame stateFrame(const std::span<const uint8_t> snapshot, const Status status, const uint16_t ver = 0x0A0B) {
  Frame f;
  f.header = headerOf(Type::State, 7, 9);
  f.state.ver = ver;
  f.state.status = status;
  f.state.snapshot = snapshot;
  return f;
}

Frame playingState() {
  Status s;
  s.turn = 2;
  return stateFrame(SNAP_TEXT, s);
}

Frame overState() {
  Status s;
  s.over = true;
  s.turn = 0;
  s.winners = 5;
  Frame f = stateFrame(SNAP_ONE, s, 1);
  f.header.seq = 0;
  f.header.ack = 1;
  return f;
}

Frame abortFrame() {
  Frame f;
  f.header = headerOf(Type::Abort);
  f.abortReason = AbortReason::VersionMismatch;
  return f;
}

Frame pingFrame() {
  Frame f;
  f.header = headerOf(Type::Ping, 0xFFFF, 0xFFFF);
  return f;
}

Frame ackFrame() {
  Frame f;
  f.header = headerOf(Type::Ack, 0, 0x0102);
  return f;
}

bool sameBytes(const std::span<const uint8_t> a, const std::span<const uint8_t> b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

bool sameFrame(const Frame& a, const Frame& b) {
  if (a.header.type != b.header.type || a.header.session != b.header.session || a.header.seq != b.header.seq ||
      a.header.ack != b.header.ack) {
    return false;
  }
  switch (a.header.type) {
    case Type::Advert:
      return a.advert.gameId == b.advert.gameId && a.advert.pkgHash == b.advert.pkgHash &&
             a.advert.manifestApi == b.advert.manifestApi && a.advert.hostApi == b.advert.hostApi &&
             a.advert.surface == b.advert.surface && a.advert.seatsMax == b.advert.seatsMax &&
             a.advert.seatsTaken == b.advert.seatsTaken && sameBytes(a.advert.hostLabel, b.advert.hostLabel);
    case Type::Join:
      return a.join.pkgHash == b.join.pkgHash && a.join.hostApi == b.join.hostApi && a.join.surface == b.join.surface;
    case Type::Accept:
      return a.accept.seat == b.accept.seat && a.accept.n == b.accept.n;
    case Type::State:
      return a.state.ver == b.state.ver && a.state.status.over == b.state.status.over &&
             a.state.status.turn == b.state.status.turn && a.state.status.winners == b.state.status.winners &&
             sameBytes(a.state.snapshot, b.state.snapshot);
    case Type::Abort:
      return a.abortReason == b.abortReason;
    case Type::Ping:
    case Type::Ack:
      return true;
  }
  return false;
}

Bytes encoded(const Frame& frame) {
  Bytes out(MAX_FRAME_BYTES, 0xCC);
  const size_t n = encode(out, frame);
  out.resize(n);
  return out;
}

DecodeStatus decoded(const Bytes& bytes, Frame& out, const uint32_t session = ANY_SESSION) {
  return decode(bytes, session, out);
}

DecodeStatus decodedStatus(const Bytes& bytes, const uint32_t session = ANY_SESSION) {
  Frame scratch;
  return decode(bytes, session, scratch);
}

Bytes with(Bytes bytes, const size_t at, const uint8_t value) {
  bytes[at] = value;
  return bytes;
}

struct Golden {
  const char* name;
  Bytes bytes;
  Frame frame;
};

// Written by hand from the tables in Protocol.h (and the plan's worked examples), never from encode.
std::vector<Golden> goldens() {
  std::vector<Golden> g;
  g.push_back({"advert",
               {0x58, 0x48, 0x01, 0x01, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x00, 0x00,  //
                0x03, 0x74, 0x69, 0x63,                                                  // L=3 "tic"
                0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,                          // pkgHash
                0x01, 0x01,                                                              // manifestApi, hostApi
                0x84, 0x9b, 0x04, 0xee,                                                  // surface
                0x02, 0x01,                                                              // seatsMax, seatsTaken
                0x07, 0x58, 0x34, 0x20, 0x33, 0x46, 0x32, 0x41},                         // M=7 "X4 3F2A"
               advertFrame()});
  g.push_back({"join",
               {0x58, 0x48, 0x01, 0x02, 0x44, 0x33, 0x22, 0x11, 0x05, 0x00, 0x06, 0x00,  //
                0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,                          // pkgHash
                0x02,                                                                    // hostApi
                0x04, 0x03, 0x02, 0x01},                                                 // surface
               joinFrame()});
  g.push_back(
      {"accept", {0x58, 0x48, 0x01, 0x03, 0x44, 0x33, 0x22, 0x11, 0x02, 0x01, 0x04, 0x03, 0x02, 0x02}, acceptFrame()});
  g.push_back({"state-playing",
               {0x58, 0x48, 0x01, 0x04, 0x44, 0x33, 0x22, 0x11, 0x07, 0x00, 0x09, 0x00,  //
                0x0b, 0x0a,                                                              // ver
                0x02, 0x00,                                                              // turn, over
                0x00, 0x00,                                                              // winners
                0x73, 0x6e, 0x61, 0x70},                                                 // "snap"
               playingState()});
  g.push_back({"state-over",
               {0x58, 0x48, 0x01, 0x04, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x01, 0x00,  //
                0x01, 0x00,                                                              // ver
                0x00, 0x01,                                                              // turn 0, over 1
                0x05, 0x00,                                                              // winners: seats 1 and 3
                0xff},
               overState()});
  g.push_back({"abort", {0x58, 0x48, 0x01, 0x05, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05}, abortFrame()});
  g.push_back({"ping", {0x58, 0x48, 0x01, 0x06, 0x44, 0x33, 0x22, 0x11, 0xff, 0xff, 0xff, 0xff}, pingFrame()});
  g.push_back({"ack", {0x58, 0x48, 0x01, 0x07, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x02, 0x01}, ackFrame()});
  return g;
}

Bytes pattern(const size_t size, const uint8_t seed) {
  Bytes bytes(size);
  for (size_t i = 0; i < size; i++) bytes[i] = static_cast<uint8_t>(seed + i * 13);
  return bytes;
}

// A hand-laid ADVERT with the given lengths, bypassing encode's limits, so decode's own checks are tested.
Bytes craftAdvert(const size_t idLen, const size_t labelLen, const uint8_t idByte = 'g') {
  Bytes b = {0x58, 0x48, 0x01, 0x01, 0x44, 0x33, 0x22, 0x11, 0, 0, 0, 0};
  b.push_back(static_cast<uint8_t>(idLen));
  b.insert(b.end(), idLen, idByte);
  b.insert(b.end(), {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0x01, 0x01, 0x84, 0x9b, 0x04, 0xee, 0x02, 0x01});
  b.push_back(static_cast<uint8_t>(labelLen));
  b.insert(b.end(), labelLen, 'L');
  return b;
}

// A hand-laid STATE (playing, seat 1, over 0) with a snapshot of `size` bytes.
Bytes craftState(const size_t size) {
  Bytes b = {0x58, 0x48, 0x01, 0x04, 0x44, 0x33, 0x22, 0x11, 0, 0, 0, 0, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00};
  const Bytes snap = pattern(size, 3);
  b.insert(b.end(), snap.begin(), snap.end());
  return b;
}

bool inside(const void* const p, const size_t size, const Bytes& in) {
  const auto* const begin = reinterpret_cast<const uint8_t*>(p);
  return begin >= in.data() && begin + size <= in.data() + in.size();
}

uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

TEST(ProtocolTest, GoldenFramesEncodeToTheHandWrittenBytes) {
  for (const Golden& g : goldens()) {
    SCOPED_TRACE(g.name);
    EXPECT_EQ(encoded(g.frame), g.bytes);
  }
}

TEST(ProtocolTest, GoldenFramesDecodeToTheExpectedFrame) {
  for (const Golden& g : goldens()) {
    SCOPED_TRACE(g.name);
    Frame out;
    ASSERT_EQ(decoded(g.bytes, out), DecodeStatus::Ok);
    EXPECT_TRUE(sameFrame(out, g.frame));
    EXPECT_EQ(encoded(out), g.bytes) << "re-encoding the decoded frame must give identical bytes";
    // The same frame passes the filter of its own session.
    EXPECT_EQ(decodedStatus(g.bytes, SESSION), DecodeStatus::Ok);
  }
}

TEST(ProtocolTest, GoldenSizesMatchTheTables) {
  const std::vector<Golden> all = goldens();
  const size_t expected[] = {40, 25, 14, 22, 19, 13, 12, 12};
  ASSERT_EQ(all.size(), std::size(expected));
  for (size_t i = 0; i < all.size(); i++) EXPECT_EQ(all[i].bytes.size(), expected[i]) << all[i].name;
}

TEST(ProtocolTest, DecodedSpansAliasTheInput) {
  const Bytes advert = goldens()[0].bytes;
  Frame out;
  ASSERT_EQ(decoded(advert, out), DecodeStatus::Ok);
  EXPECT_EQ(static_cast<const void*>(out.advert.gameId.data()), static_cast<const void*>(advert.data() + 13));
  EXPECT_EQ(static_cast<const void*>(out.advert.hostLabel.data()), static_cast<const void*>(advert.data() + 33));
  const Bytes state = goldens()[3].bytes;
  ASSERT_EQ(decoded(state, out), DecodeStatus::Ok);
  EXPECT_EQ(static_cast<const void*>(out.state.snapshot.data()), static_cast<const void*>(state.data() + 18));
}

TEST(ProtocolTest, EveryTypeRoundTripsAtItsLimits) {
  const Bytes id32(MAX_GAME_ID_BYTES, 'g');
  const Bytes label16 = pattern(MAX_HOST_LABEL_BYTES, 0x40);
  const uint32_t sessions[] = {1, SESSION, 0xFFFFFFFFu};
  const uint16_t counters[] = {0, 1, 0xFFFF};

  std::vector<Frame> frames;
  Frame advert = advertFrame();
  advert.advert.gameId = std::string_view(reinterpret_cast<const char*>(id32.data()), id32.size());
  advert.advert.hostLabel = label16;
  advert.advert.seatsMax = 16;
  advert.advert.seatsTaken = 16;
  advert.advert.surface = 0xFFFFFFFFu;
  frames.push_back(advert);
  Frame empty = advertFrame();  // gameId and label may both be empty
  empty.advert.gameId = std::string_view();
  empty.advert.hostLabel = std::span<const uint8_t>();
  empty.advert.seatsTaken = 0;
  empty.advert.seatsMax = 1;
  frames.push_back(empty);
  Frame join = joinFrame();
  join.join.surface = 0xFFFFFFFFu;
  join.join.hostApi = 255;
  frames.push_back(join);
  for (uint8_t n = 1; n <= 16; n++) {
    Frame accept = acceptFrame();
    accept.accept.n = n;
    accept.accept.seat = n;
    frames.push_back(accept);
    accept.accept.seat = 1;
    frames.push_back(accept);
  }
  const Bytes one = pattern(1, 9);
  const Bytes full = pattern(MAX_SNAPSHOT_BYTES, 17);
  Status playing;
  playing.turn = 16;
  Status over;
  over.over = true;
  over.winners = 0xFFFF;
  Status draw;
  draw.over = true;
  for (const Bytes* snap : {&one, &full}) {
    for (const Status status : {playing, over, draw}) {
      frames.push_back(stateFrame(*snap, status, 0xFFFF));
      frames.push_back(stateFrame(*snap, status, 0));
    }
  }
  for (uint8_t reason = 1; reason <= 5; reason++) {
    Frame abort = abortFrame();
    abort.abortReason = static_cast<AbortReason>(reason);
    frames.push_back(abort);
  }
  frames.push_back(pingFrame());
  frames.push_back(ackFrame());

  for (Frame frame : frames) {
    for (const uint32_t session : sessions) {
      for (const uint16_t counter : counters) {
        frame.header.session = session;
        frame.header.seq = counter;
        frame.header.ack = static_cast<uint16_t>(~counter);
        SCOPED_TRACE(static_cast<int>(frame.header.type));
        const Bytes bytes = encoded(frame);
        ASSERT_FALSE(bytes.empty());
        Frame out;
        ASSERT_EQ(decoded(bytes, out, session), DecodeStatus::Ok);
        EXPECT_TRUE(sameFrame(out, frame));
        EXPECT_EQ(encoded(out), bytes);
      }
    }
  }
}

TEST(ProtocolTest, LargestFrameIsFourteenEighteenAndFitsTheCeiling) {
  Status s;
  s.turn = 1;
  const Bytes snap = pattern(MAX_SNAPSHOT_BYTES, 1);
  const Bytes bytes = encoded(stateFrame(snap, s));
  EXPECT_EQ(bytes.size(), 1418u);
  EXPECT_EQ(bytes.size(), MAX_STATE_FRAME_BYTES);
  EXPECT_LE(bytes.size(), MAX_FRAME_BYTES);
  Frame out;
  ASSERT_EQ(decoded(bytes, out), DecodeStatus::Ok);
  EXPECT_EQ(out.state.snapshot.size(), MAX_SNAPSHOT_BYTES);
  // The hand-laid 1,400 B STATE (no encode involved) decodes and re-encodes to itself.
  const Bytes crafted = craftState(MAX_SNAPSHOT_BYTES);
  ASSERT_EQ(decoded(crafted, out), DecodeStatus::Ok);
  EXPECT_EQ(encoded(out), crafted);
  // The largest other frame is the longest ADVERT: 12 + 18 + 32 + 16.
  Bytes id(MAX_GAME_ID_BYTES, 'g');
  Frame advert = advertFrame();
  advert.advert.gameId = std::string_view(reinterpret_cast<const char*>(id.data()), id.size());
  const Bytes label = pattern(MAX_HOST_LABEL_BYTES, 1);
  advert.advert.hostLabel = label;
  EXPECT_EQ(encoded(advert).size(), 78u);
}

TEST(ProtocolTest, ShortFramesAreRefused) {
  EXPECT_EQ(decodedStatus(Bytes()), DecodeStatus::TooShort);
  EXPECT_EQ(decodedStatus(Bytes(11, 0x58)), DecodeStatus::TooShort);
  // A header-only STATE has no snapshot.
  Bytes headerOnly = goldens()[3].bytes;
  headerOnly.resize(HEADER_BYTES);
  EXPECT_EQ(decodedStatus(headerOnly), DecodeStatus::BadPayload);
  // A header-only frame of each type that needs a payload.
  for (const Golden& g : goldens()) {
    Bytes b = g.bytes;
    b.resize(HEADER_BYTES);
    const bool empty = g.frame.header.type == Type::Ping || g.frame.header.type == Type::Ack;
    EXPECT_EQ(decodedStatus(b), empty ? DecodeStatus::Ok : DecodeStatus::BadPayload) << g.name;
  }
}

TEST(ProtocolTest, EveryProperPrefixOfAGoldenFrameIsRefused) {
  for (const Golden& g : goldens()) {
    SCOPED_TRACE(g.name);
    // A STATE's snapshot is the rest of the frame, so a prefix with at least one snapshot byte is a valid shorter
    // STATE (checked below); every other prefix is short or has a payload of the wrong length.
    const size_t refusedUpTo = g.frame.header.type == Type::State ? HEADER_BYTES + 6 : g.bytes.size() - 1;
    for (size_t n = 0; n <= refusedUpTo; n++) {
      const Bytes prefix(g.bytes.begin(), g.bytes.begin() + static_cast<std::ptrdiff_t>(n));
      const DecodeStatus status = decodedStatus(prefix);
      EXPECT_EQ(status, n < HEADER_BYTES ? DecodeStatus::TooShort : DecodeStatus::BadPayload) << "prefix " << n;
    }
  }
}

TEST(ProtocolTest, AStatePrefixWithSnapshotBytesIsAShorterState) {
  const Bytes full = goldens()[3].bytes;
  for (size_t n = HEADER_BYTES + 7; n < full.size(); n++) {
    const Bytes prefix(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(n));
    Frame out;
    ASSERT_EQ(decoded(prefix, out), DecodeStatus::Ok) << n;
    EXPECT_EQ(out.state.snapshot.size(), n - HEADER_BYTES - 6);
  }
}

TEST(ProtocolTest, AFrameOneByteLongerIsRefusedUnlessItsPayloadIsTheRestOfTheFrame) {
  for (const Golden& g : goldens()) {
    SCOPED_TRACE(g.name);
    Bytes longer = g.bytes;
    longer.push_back(0x00);
    Frame out;
    if (g.frame.header.type == Type::State) {
      ASSERT_EQ(decoded(longer, out), DecodeStatus::Ok);
      EXPECT_EQ(out.state.snapshot.size(), g.frame.state.snapshot.size() + 1);
    } else {
      EXPECT_EQ(decoded(longer, out), DecodeStatus::BadPayload);
    }
  }
}

TEST(ProtocolTest, TooLongFramesAreRefusedBeforeAnythingElse) {
  EXPECT_EQ(decodedStatus(craftState(MAX_FRAME_BYTES - 18)), DecodeStatus::BadPayload);   // 1470 B: not TooLong
  EXPECT_EQ(decodedStatus(craftState(MAX_FRAME_BYTES - 18 + 1)), DecodeStatus::TooLong);  // 1471 B
  Bytes wrongMagic = craftState(MAX_FRAME_BYTES - 18 + 1);
  wrongMagic[0] = 0;
  wrongMagic[2] = 9;
  EXPECT_EQ(decodedStatus(wrongMagic), DecodeStatus::TooLong);
  EXPECT_EQ(decodedStatus(Bytes(5000, 0)), DecodeStatus::TooLong);
}

TEST(ProtocolTest, WrongMagicProtoOrSessionIsRefused) {
  const Bytes accept = goldens()[2].bytes;
  EXPECT_EQ(decodedStatus(with(accept, 0, 0x59)), DecodeStatus::BadMagic);
  EXPECT_EQ(decodedStatus(with(accept, 1, 0x49)), DecodeStatus::BadMagic);
  EXPECT_EQ(decodedStatus(with(accept, 0, 0x00)), DecodeStatus::BadMagic);
  for (const uint8_t proto : {0, 2, 255}) {
    EXPECT_EQ(decodedStatus(with(accept, 2, proto)), DecodeStatus::BadProto) << int(proto);
  }
  EXPECT_EQ(decodedStatus(accept, SESSION + 1), DecodeStatus::BadSession);
  EXPECT_EQ(decodedStatus(accept, 0xFFFFFFFFu), DecodeStatus::BadSession);
  EXPECT_EQ(decodedStatus(accept, SESSION), DecodeStatus::Ok);
  EXPECT_EQ(decodedStatus(accept, ANY_SESSION), DecodeStatus::Ok);
  // Session 0 on the wire is refused whatever the filter says.
  Bytes zero = accept;
  for (size_t i = 0; i < 4; i++) zero[SESSION_AT + i] = 0;
  EXPECT_EQ(decodedStatus(zero, ANY_SESSION), DecodeStatus::BadSession);
  EXPECT_EQ(decodedStatus(zero, SESSION), DecodeStatus::BadSession);
}

TEST(ProtocolTest, ChecksRunInTheOrderTheHeaderWrites) {
  Bytes b = goldens()[2].bytes;
  b[0] = 0;  // magic
  b[2] = 9;  // proto
  for (size_t i = 0; i < 4; i++) b[SESSION_AT + i] = 0;
  b[3] = 0;  // type
  EXPECT_EQ(decodedStatus(b), DecodeStatus::BadMagic);
  b[0] = 0x58;
  EXPECT_EQ(decodedStatus(b), DecodeStatus::BadProto);
  b[2] = 1;
  EXPECT_EQ(decodedStatus(b), DecodeStatus::BadSession);
  b[SESSION_AT] = 1;
  EXPECT_EQ(decodedStatus(b), DecodeStatus::BadType);
  // The type is checked before the payload is looked at.
  b[3] = 8;
  b.resize(HEADER_BYTES);
  EXPECT_EQ(decodedStatus(b), DecodeStatus::BadType);
}

TEST(ProtocolTest, UnknownTypeIsRefused) {
  const Bytes accept = goldens()[2].bytes;
  for (const uint8_t type : {0, 8, 9, 127, 255}) {
    EXPECT_EQ(decodedStatus(with(accept, 3, type)), DecodeStatus::BadType) << int(type);
  }
  // The retired MOVE (4 in older drafts) and REJECT (6) are not on the wire: type 4 is STATE and 6 is PING, so a
  // frame that carries a move's bytes under either is a payload error, never a move.
  EXPECT_EQ(decodedStatus(with(accept, 3, 4)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(accept, 3, 6)), DecodeStatus::BadPayload);
}

TEST(ProtocolTest, OverLongFieldsAreRefusedByEncodeAndDecode) {
  const Bytes id33(MAX_GAME_ID_BYTES + 1, 'g');
  const Bytes label17 = pattern(MAX_HOST_LABEL_BYTES + 1, 1);
  const Bytes snap1401 = pattern(MAX_SNAPSHOT_BYTES + 1, 1);

  Frame advert = advertFrame();
  advert.advert.gameId = std::string_view(reinterpret_cast<const char*>(id33.data()), id33.size());
  EXPECT_TRUE(encoded(advert).empty());
  advert = advertFrame();
  advert.advert.hostLabel = label17;
  EXPECT_TRUE(encoded(advert).empty());
  Status playing;
  playing.turn = 1;
  EXPECT_TRUE(encoded(stateFrame(snap1401, playing)).empty());
  EXPECT_TRUE(encoded(stateFrame(std::span<const uint8_t>(), playing)).empty()) << "an empty snapshot";

  EXPECT_EQ(decodedStatus(craftAdvert(33, 7)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(craftAdvert(3, 17)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(craftState(MAX_SNAPSHOT_BYTES + 1)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(craftState(0)), DecodeStatus::BadPayload);
  // The limits themselves are accepted.
  EXPECT_EQ(decodedStatus(craftAdvert(32, 16)), DecodeStatus::Ok);
  EXPECT_EQ(decodedStatus(craftAdvert(0, 0)), DecodeStatus::Ok);
  EXPECT_EQ(decodedStatus(craftState(MAX_SNAPSHOT_BYTES)), DecodeStatus::Ok);
  EXPECT_EQ(decodedStatus(craftState(1)), DecodeStatus::Ok);
}

TEST(ProtocolTest, MalformedValuesAreRefusedByDecode) {
  const std::vector<Golden> all = goldens();
  const Bytes& advert = all[0].bytes;
  const Bytes& join = all[1].bytes;
  const Bytes& accept = all[2].bytes;
  const Bytes& playing = all[3].bytes;
  const Bytes& over = all[4].bytes;
  const Bytes& abort = all[5].bytes;
  const Bytes& ping = all[6].bytes;

  // ABORT reasons: 1..5 only.
  EXPECT_EQ(decodedStatus(with(abort, PAYLOAD_AT, 0)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(abort, PAYLOAD_AT, 6)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(abort, PAYLOAD_AT, 255)), DecodeStatus::BadPayload);
  for (uint8_t reason = 1; reason <= 5; reason++) {
    EXPECT_EQ(decodedStatus(with(abort, PAYLOAD_AT, reason)), DecodeStatus::Ok) << int(reason);
  }
  // STATE: over is 0 or 1; over 1 needs turn 0; over 0 needs a turn 1..16 and no winners.
  EXPECT_EQ(decodedStatus(with(playing, PAYLOAD_AT + 3, 2)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(playing, PAYLOAD_AT + 3, 255)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(playing, PAYLOAD_AT + 2, 0)), DecodeStatus::BadPayload) << "not over, turn 0";
  EXPECT_EQ(decodedStatus(with(playing, PAYLOAD_AT + 2, 17)), DecodeStatus::BadPayload) << "turn above 16";
  EXPECT_EQ(decodedStatus(with(playing, PAYLOAD_AT + 2, 16)), DecodeStatus::Ok);
  EXPECT_EQ(decodedStatus(with(playing, PAYLOAD_AT + 4, 1)), DecodeStatus::BadPayload) << "not over, winners";
  EXPECT_EQ(decodedStatus(with(playing, PAYLOAD_AT + 5, 1)), DecodeStatus::BadPayload) << "not over, winners";
  EXPECT_EQ(decodedStatus(with(over, PAYLOAD_AT + 2, 1)), DecodeStatus::BadPayload) << "over with a turn";
  // ACCEPT: seat 1..n, n up to 16.
  EXPECT_EQ(decodedStatus(with(accept, PAYLOAD_AT, 0)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(accept, PAYLOAD_AT, 3)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(with(accept, PAYLOAD_AT, 1), PAYLOAD_AT + 1, 0)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(with(accept, PAYLOAD_AT, 1), PAYLOAD_AT + 1, 17)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(with(accept, PAYLOAD_AT, 16), PAYLOAD_AT + 1, 16)), DecodeStatus::Ok);
  // ADVERT: seatsMax 1..16 and seatsTaken <= seatsMax (L = 3, so they sit at payload offsets 15 + 3 and 16 + 3).
  const size_t maxAt = PAYLOAD_AT + 18;
  EXPECT_EQ(decodedStatus(with(advert, maxAt + 1, 3)), DecodeStatus::BadPayload) << "taken above max";
  EXPECT_EQ(decodedStatus(with(with(advert, maxAt, 0), maxAt + 1, 0)), DecodeStatus::BadPayload) << "no seats";
  EXPECT_EQ(decodedStatus(with(advert, maxAt, 17)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(with(advert, maxAt, 16), maxAt + 1, 16)), DecodeStatus::Ok);
  // ADVERT lengths that disagree with the frame.
  EXPECT_EQ(decodedStatus(with(advert, PAYLOAD_AT, 2)), DecodeStatus::BadPayload) << "gameIdLen short";
  EXPECT_EQ(decodedStatus(with(advert, PAYLOAD_AT, 4)), DecodeStatus::BadPayload) << "gameIdLen long";
  EXPECT_EQ(decodedStatus(with(advert, PAYLOAD_AT, 255)), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(with(advert, PAYLOAD_AT + 20, 6)), DecodeStatus::BadPayload) << "labelLen short";
  EXPECT_EQ(decodedStatus(with(advert, PAYLOAD_AT + 20, 8)), DecodeStatus::BadPayload) << "labelLen long";
  // Fixed payloads take no trailing byte and no missing one.
  Bytes join26 = join;
  join26.push_back(0);
  EXPECT_EQ(decodedStatus(join26), DecodeStatus::BadPayload);
  EXPECT_EQ(decodedStatus(Bytes(join.begin(), join.end() - 1)), DecodeStatus::BadPayload);
  Bytes ping13 = ping;
  ping13.push_back(0);
  EXPECT_EQ(decodedStatus(ping13), DecodeStatus::BadPayload);
}

TEST(ProtocolTest, EncodeRefusesAFrameNoSenderCouldProduce) {
  auto refused = [](const Frame& f) { return encoded(f).empty(); };

  Frame f = advertFrame();
  f.header.session = 0;
  EXPECT_TRUE(refused(f)) << "session 0 is never on the wire";
  f = pingFrame();
  f.header.session = 0;
  EXPECT_TRUE(refused(f));

  f = pingFrame();
  f.header.type = static_cast<Type>(0);
  EXPECT_TRUE(refused(f));
  f.header.type = static_cast<Type>(8);
  EXPECT_TRUE(refused(f));
  f.header.type = static_cast<Type>(255);
  EXPECT_TRUE(refused(f));

  f = abortFrame();
  f.abortReason = static_cast<AbortReason>(0);
  EXPECT_TRUE(refused(f));
  f.abortReason = static_cast<AbortReason>(6);
  EXPECT_TRUE(refused(f));

  f = acceptFrame();
  f.accept.seat = 0;
  EXPECT_TRUE(refused(f));
  f = acceptFrame();
  f.accept.seat = 3;
  EXPECT_TRUE(refused(f));
  f = acceptFrame();
  f.accept.n = 17;
  EXPECT_TRUE(refused(f));

  f = advertFrame();
  f.advert.seatsTaken = 3;
  EXPECT_TRUE(refused(f));
  f = advertFrame();
  f.advert.seatsMax = 0;
  f.advert.seatsTaken = 0;
  EXPECT_TRUE(refused(f));
  f = advertFrame();
  f.advert.seatsMax = 17;
  EXPECT_TRUE(refused(f));

  f = playingState();
  f.state.status.turn = 0;
  EXPECT_TRUE(refused(f)) << "not over, turn 0";
  f = playingState();
  f.state.status.turn = 17;
  EXPECT_TRUE(refused(f));
  f = playingState();
  f.state.status.winners = 1;
  EXPECT_TRUE(refused(f)) << "not over, winners";
  f = overState();
  f.state.status.turn = 1;
  EXPECT_TRUE(refused(f)) << "over with a turn";
}

TEST(ProtocolTest, EncodeIntoATooSmallBufferWritesNothing) {
  for (const Golden& g : goldens()) {
    SCOPED_TRACE(g.name);
    const size_t size = g.bytes.size();
    Bytes out(size, 0xCC);
    EXPECT_EQ(encode(std::span<uint8_t>(out.data(), size - 1), g.frame), 0u);
    EXPECT_EQ(out, Bytes(size, 0xCC)) << "a refused encode must not write";
    EXPECT_EQ(encode(std::span<uint8_t>(out.data(), 0), g.frame), 0u);
    EXPECT_EQ(encode(out, g.frame), size) << "an exactly sized buffer is enough";
    EXPECT_EQ(out, g.bytes);
  }
  // A refused field writes nothing either.
  Bytes out(MAX_FRAME_BYTES, 0xCC);
  Frame bad = acceptFrame();
  bad.accept.seat = 0;
  EXPECT_EQ(encode(out, bad), 0u);
  EXPECT_EQ(out, Bytes(MAX_FRAME_BYTES, 0xCC));
  // encode never writes past the frame it returns.
  Frame ping = pingFrame();
  EXPECT_EQ(encode(out, ping), HEADER_BYTES);
  EXPECT_EQ(out[HEADER_BYTES], 0xCC);
}

TEST(ProtocolTest, EncodeWritesTheProtoItsDecodeAccepts) { EXPECT_EQ(encoded(pingFrame())[2], PROTO); }

// Checks one decoded frame: accepted, it points only into `in` and re-encodes to the same bytes.
void expectCanonical(const Bytes& in) {
  Frame out;
  if (decode(in, ANY_SESSION, out) != DecodeStatus::Ok) return;
  switch (out.header.type) {
    case Type::Advert:
      ASSERT_TRUE(inside(out.advert.gameId.data(), out.advert.gameId.size(), in));
      ASSERT_TRUE(inside(out.advert.hostLabel.data(), out.advert.hostLabel.size(), in));
      break;
    case Type::State:
      ASSERT_TRUE(inside(out.state.snapshot.data(), out.state.snapshot.size(), in));
      break;
    default:
      break;
  }
  EXPECT_EQ(encoded(out), in) << "an accepted frame must have exactly one encoding";
}

TEST(ProtocolTest, EverySingleByteMutationOfAGoldenFrameIsRefusedOrCanonical) {
  for (const Golden& g : goldens()) {
    SCOPED_TRACE(g.name);
    for (size_t at = 0; at < g.bytes.size(); at++) {
      for (int value = 0; value < 256; value++) {
        expectCanonical(with(g.bytes, at, static_cast<uint8_t>(value)));
        if (::testing::Test::HasFatalFailure()) return;
      }
    }
  }
}

TEST(ProtocolTest, SeededMutationsNeverReadOutsideTheSpanAndAcceptedFramesAreCanonical) {
  std::vector<Bytes> seeds;
  for (const Golden& g : goldens()) seeds.push_back(g.bytes);
  seeds.push_back(craftState(MAX_SNAPSHOT_BYTES));
  seeds.push_back(craftAdvert(32, 16));
  uint32_t state = 0xC0FFEE;
  for (int round = 0; round < 40000; round++) {
    Bytes frame = seeds[nextRandom(state) % seeds.size()];
    const int edits = 1 + static_cast<int>(nextRandom(state) % 3);
    for (int i = 0; i < edits; i++) frame[nextRandom(state) % frame.size()] = static_cast<uint8_t>(nextRandom(state));
    // Sometimes cut or extend it. Each frame is an exact-size vector, so a read past the span is visible when the
    // suite runs under ASan/UBSan (the default host build does not enable them; run by hand with
    // -fsanitize=address,undefined).
    const uint32_t shape = nextRandom(state) % 8;
    if (shape == 0 && frame.size() > 1) frame.resize(1 + nextRandom(state) % (frame.size() - 1));
    if (shape == 1) frame.resize(frame.size() + 1 + nextRandom(state) % 40, static_cast<uint8_t>(nextRandom(state)));
    expectCanonical(frame);
    if (::testing::Test::HasFatalFailure()) return;
  }
}

}  // namespace
