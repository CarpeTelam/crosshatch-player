#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "IGameRules.h"
#include "Roster.h"
#include "Session.h"

// The nearby-play frame (spine AD-13): the only encoder and decoder of the header and its seven payloads.
// Pure functions over caller-owned spans: no allocation, no log, no state. Everything is little-endian,
// written and read a byte at a time.
//
// A frame is header (12 B) + payload; there is no length field, the payload is the rest of the datagram (the
// link preserves boundaries).
//
// Header (HEADER_BYTES = 12):
//   0  1  magic 0x58 ('X')
//   1  1  magic 0x48 ('H')
//   2  1  proto  PROTO = 1
//   3  1  type   code below
//   4  4  session u32: the host's id, never 0 on the wire
//   8  2  seq u16: ReliableLink's; Protocol carries it and neither checks nor interprets it
//  10  2  ack u16: ReliableLink's; same
// ADVERT, PING, ACK, and ABORT consume no seq (ReliableLink writes 0 and ignores it on receipt); ack rides on
// every frame (0 on ADVERT).
//
// ADVERT (type 1; broadcast; payload 18 + L + M B, L = gameId length 0..32, M = label length 0..16):
//   0      1  gameIdLen L
//   1      L  gameId bytes (the manifest id; only the length is checked)
//   1+L    8  pkgHash
//   9+L    1  manifestApi u8 (the game's declared api)
//   10+L   1  hostApi u8 (the host's API_LEVEL)
//   11+L   4  surface u32 (API_SURFACE_CRC)
//   15+L   1  seatsMax (1..16)
//   16+L   1  seatsTaken (0..seatsMax)
//   17+L   1  labelLen M
//   18+L   M  hostLabel bytes (opaque; shown by the lobby)
//   The frame is exactly this long: a trailing byte is refused.
// JOIN (type 2; unicast guest to host; header session is the host's; payload 13 B):
//   0  8  pkgHash
//   8  1  hostApi u8 (the guest's own API_LEVEL)
//   9  4  surface u32
// ACCEPT (type 3; unicast host to each guest at Start; payload 2 B):
//   0  1  seat (1..n; the guest's)
//   1  1  n (seat count, seat <= n <= 16)
// STATE (type 4; reliable, latest-wins; payload 6 + S B, S = snapshot length 1..1400):
//   0  2  ver u16
//   2  1  turn u8 (a seat 1..16 while playing; 0 when over)
//   3  1  over u8 (0 or 1)
//   4  2  winners u16 (bit seat - 1; 0 unless over)
//   6  S  snapshot (the rest of the frame)
//   over 1 needs turn 0; over 0 needs turn 1..16 and winners 0.
// ABORT (type 5; unsequenced; payload 1 B): reason 1 left, 2 script_error, 3 timeout, 4 full,
//   5 version_mismatch (0 and above 5 refused).
// PING (type 6) and ACK (type 7): empty payload, frame 12 B, unsequenced; any payload byte is refused.
//   They differ only in meaning, which ReliableLink owns.
namespace GameCore::Protocol {

// Bumped for any change to the header, a payload, or a type code, and for any bump of
// GameScript::Codec::VERSION (AD-13: proto "includes the codec version"). lib/GameCore cannot include
// lib/GameScript, so CodecTest's ProtoIsBumpedWithTheCodecVersion pins the pair.
inline constexpr uint8_t PROTO = 1;
inline constexpr size_t HEADER_BYTES = 12;
// The whole datagram, header included; a protocol constant (entry 5 checks it against what the radio carries).
inline constexpr size_t MAX_FRAME_BYTES = 1470;
inline constexpr size_t MAX_GAME_ID_BYTES = 32;
inline constexpr size_t MAX_HOST_LABEL_BYTES = 16;
inline constexpr size_t PKG_HASH_BYTES = 8;
inline constexpr size_t MAX_SNAPSHOT_BYTES = SNAPSHOT_BYTES;                       // 1400 (Session.h)
inline constexpr size_t MAX_PAYLOAD_BYTES = 6 + MAX_SNAPSHOT_BYTES;                // 1406, STATE's
inline constexpr size_t MAX_STATE_FRAME_BYTES = HEADER_BYTES + MAX_PAYLOAD_BYTES;  // 1418
static_assert(MAX_STATE_FRAME_BYTES <= MAX_FRAME_BYTES);
inline constexpr uint32_t ANY_SESSION = 0;  // decode's filter: accept any session

enum class Type : uint8_t { Advert = 1, Join = 2, Accept = 3, State = 4, Abort = 5, Ping = 6, Ack = 7 };
enum class AbortReason : uint8_t { Left = 1, ScriptError = 2, Timeout = 3, Full = 4, VersionMismatch = 5 };
enum class DecodeStatus : uint8_t { Ok, TooShort, TooLong, BadMagic, BadProto, BadSession, BadType, BadPayload };

struct Header {
  Type type = Type::Ping;
  uint32_t session = 0;
  uint16_t seq = 0;
  uint16_t ack = 0;
};
struct Advert {
  std::string_view gameId;
  std::array<uint8_t, PKG_HASH_BYTES> pkgHash{};
  uint8_t manifestApi = 0;
  uint8_t hostApi = 0;
  uint32_t surface = 0;
  uint8_t seatsMax = 0;
  uint8_t seatsTaken = 0;
  std::span<const uint8_t> hostLabel;
};
struct Join {
  std::array<uint8_t, PKG_HASH_BYTES> pkgHash{};
  uint8_t hostApi = 0;
  uint32_t surface = 0;
};
struct Accept {
  uint8_t seat = 0;
  uint8_t n = 0;
};
struct State {
  uint16_t ver = 0;
  Status status;
  std::span<const uint8_t> snapshot;
};
// Only the member for header.type is read by encode or set by decode (Abort: abortReason; Ping, Ack: none).
struct Frame {
  Header header;
  Advert advert;
  Join join;
  Accept accept;
  State state;
  AbortReason abortReason = AbortReason::Left;
};

// Writes `frame` to `out`; returns its size, or 0 and writes nothing when it does not fit `out`, a field is
// over its limit or out of range, or header.session is 0. Writes the header's proto as PROTO. `out` must not
// overlap any span or view in `frame` (encode copies forward a byte at a time).
size_t encode(std::span<uint8_t> out, const Frame& frame);
// Reads `in` as one frame. Checks in this order, returning the first failure: length >= 12 (TooShort), <= 1470
// (TooLong), magic, proto (BadProto), session (BadSession: session 0 on the wire, or `session` != ANY_SESSION
// and != the frame's), type (BadType), then the payload's exact length, field limits, and value ranges
// (BadPayload). On Ok `out` is set; on any failure it is unspecified. Spans and the gameId view in `out`
// alias `in` and live as long as it does. Never reads outside `in`.
DecodeStatus decode(std::span<const uint8_t> in, uint32_t session, Frame& out);

}  // namespace GameCore::Protocol
