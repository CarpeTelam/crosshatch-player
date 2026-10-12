#include "Protocol.h"

namespace GameCore::Protocol {

namespace {

constexpr uint8_t MAGIC_0 = 0x58;          // 'X'
constexpr uint8_t MAGIC_1 = 0x48;          // 'H'
constexpr size_t ADVERT_FIXED_BYTES = 18;  // an ADVERT's payload without gameId and hostLabel
constexpr size_t JOIN_PAYLOAD_BYTES = PKG_HASH_BYTES + 1 + 4;
constexpr size_t ACCEPT_PAYLOAD_BYTES = 2;
constexpr size_t STATE_FIXED_BYTES = 6;  // a STATE's payload without the snapshot
constexpr size_t ABORT_PAYLOAD_BYTES = 1;

void put16(uint8_t* const at, const uint16_t value) {
  at[0] = static_cast<uint8_t>(value & 0xFF);
  at[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t* const at, const uint32_t value) {
  for (size_t i = 0; i < 4; i++) at[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

uint16_t get16(const uint8_t* const at) { return static_cast<uint16_t>(at[0] | (at[1] << 8)); }

uint32_t get32(const uint8_t* const at) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; i++) value |= static_cast<uint32_t>(at[i]) << (8 * i);
  return value;
}

bool validType(const uint8_t code) {
  return code >= static_cast<uint8_t>(Type::Advert) && code <= static_cast<uint8_t>(Type::Ack);
}

bool validAbortReason(const uint8_t code) {
  return code >= static_cast<uint8_t>(AbortReason::Left) && code <= static_cast<uint8_t>(AbortReason::VersionMismatch);
}

bool validSeats(const uint8_t seatsMax, const uint8_t seatsTaken) {
  return seatsMax >= 1 && seatsMax <= Roster::MAX_SEATS && seatsTaken <= seatsMax;
}

// A status no sender can produce is refused both ways (IGameRules.h's Status): over needs turn 0; playing
// needs a turn seat and no winners.
bool validStatus(const Status& status) {
  if (status.over) return status.turn == 0;
  return status.turn >= 1 && status.turn <= Roster::MAX_SEATS && status.winners == 0;
}

// The payload size `frame` encodes to, or SIZE_MAX when a field is over its limit or out of range.
size_t payloadSize(const Frame& frame) {
  switch (frame.header.type) {
    case Type::Advert: {
      const Advert& a = frame.advert;
      if (a.gameId.size() > MAX_GAME_ID_BYTES || a.hostLabel.size() > MAX_HOST_LABEL_BYTES) return SIZE_MAX;
      if (!validSeats(a.seatsMax, a.seatsTaken)) return SIZE_MAX;
      return ADVERT_FIXED_BYTES + a.gameId.size() + a.hostLabel.size();
    }
    case Type::Join:
      return JOIN_PAYLOAD_BYTES;
    case Type::Accept:
      if (frame.accept.seat < 1 || frame.accept.seat > frame.accept.n || frame.accept.n > Roster::MAX_SEATS) {
        return SIZE_MAX;
      }
      return ACCEPT_PAYLOAD_BYTES;
    case Type::State: {
      const State& s = frame.state;
      if (s.snapshot.empty() || s.snapshot.size() > MAX_SNAPSHOT_BYTES || !validStatus(s.status)) return SIZE_MAX;
      return STATE_FIXED_BYTES + s.snapshot.size();
    }
    case Type::Abort:
      return validAbortReason(static_cast<uint8_t>(frame.abortReason)) ? ABORT_PAYLOAD_BYTES : SIZE_MAX;
    case Type::Ping:
    case Type::Ack:
      return 0;
  }
  return SIZE_MAX;  // a type code outside the enum
}

void copyBytes(uint8_t* const to, const uint8_t* const from, const size_t count) {
  for (size_t i = 0; i < count; i++) to[i] = from[i];
}

DecodeStatus decodeAdvert(const uint8_t* const p, const size_t size, Frame& f) {
  if (size < 1) return DecodeStatus::BadPayload;
  const size_t idLen = p[0];
  if (idLen > MAX_GAME_ID_BYTES || size < ADVERT_FIXED_BYTES + idLen) return DecodeStatus::BadPayload;
  const size_t labelLen = p[17 + idLen];
  if (labelLen > MAX_HOST_LABEL_BYTES || size != ADVERT_FIXED_BYTES + idLen + labelLen) return DecodeStatus::BadPayload;
  Advert& a = f.advert;
  a.seatsMax = p[15 + idLen];
  a.seatsTaken = p[16 + idLen];
  if (!validSeats(a.seatsMax, a.seatsTaken)) return DecodeStatus::BadPayload;
  a.gameId = std::string_view(reinterpret_cast<const char*>(p + 1), idLen);
  copyBytes(a.pkgHash.data(), p + 1 + idLen, PKG_HASH_BYTES);
  a.manifestApi = p[9 + idLen];
  a.hostApi = p[10 + idLen];
  a.surface = get32(p + 11 + idLen);
  a.hostLabel = std::span<const uint8_t>(p + ADVERT_FIXED_BYTES + idLen, labelLen);
  return DecodeStatus::Ok;
}

DecodeStatus decodeState(const uint8_t* const p, const size_t size, Frame& f) {
  if (size <= STATE_FIXED_BYTES || size > MAX_PAYLOAD_BYTES) return DecodeStatus::BadPayload;
  State& s = f.state;
  s.ver = get16(p);
  s.status.turn = p[2];
  if (p[3] > 1) return DecodeStatus::BadPayload;
  s.status.over = p[3] == 1;
  s.status.winners = get16(p + 4);
  if (!validStatus(s.status)) return DecodeStatus::BadPayload;
  s.snapshot = std::span<const uint8_t>(p + STATE_FIXED_BYTES, size - STATE_FIXED_BYTES);
  return DecodeStatus::Ok;
}

DecodeStatus decodePayload(const uint8_t* const p, const size_t size, Frame& f) {
  switch (f.header.type) {
    case Type::Advert:
      return decodeAdvert(p, size, f);
    case Type::Join:
      if (size != JOIN_PAYLOAD_BYTES) return DecodeStatus::BadPayload;
      copyBytes(f.join.pkgHash.data(), p, PKG_HASH_BYTES);
      f.join.hostApi = p[8];
      f.join.surface = get32(p + 9);
      return DecodeStatus::Ok;
    case Type::Accept:
      if (size != ACCEPT_PAYLOAD_BYTES) return DecodeStatus::BadPayload;
      f.accept.seat = p[0];
      f.accept.n = p[1];
      if (f.accept.seat < 1 || f.accept.seat > f.accept.n || f.accept.n > Roster::MAX_SEATS) {
        return DecodeStatus::BadPayload;
      }
      return DecodeStatus::Ok;
    case Type::State:
      return decodeState(p, size, f);
    case Type::Abort:
      if (size != ABORT_PAYLOAD_BYTES || !validAbortReason(p[0])) return DecodeStatus::BadPayload;
      f.abortReason = static_cast<AbortReason>(p[0]);
      return DecodeStatus::Ok;
    case Type::Ping:
    case Type::Ack:
      return size == 0 ? DecodeStatus::Ok : DecodeStatus::BadPayload;
  }
  return DecodeStatus::BadType;
}

}  // namespace

size_t encode(const std::span<uint8_t> out, const Frame& frame) {
  if (frame.header.session == 0) return 0;
  const size_t payload = payloadSize(frame);
  if (payload == SIZE_MAX) return 0;
  const size_t total = HEADER_BYTES + payload;
  if (total > MAX_FRAME_BYTES || total > out.size()) return 0;

  uint8_t* const o = out.data();
  o[0] = MAGIC_0;
  o[1] = MAGIC_1;
  o[2] = PROTO;
  o[3] = static_cast<uint8_t>(frame.header.type);
  put32(o + 4, frame.header.session);
  put16(o + 8, frame.header.seq);
  put16(o + 10, frame.header.ack);
  uint8_t* const p = o + HEADER_BYTES;

  switch (frame.header.type) {
    case Type::Advert: {
      const Advert& a = frame.advert;
      const size_t idLen = a.gameId.size();
      p[0] = static_cast<uint8_t>(idLen);
      copyBytes(p + 1, reinterpret_cast<const uint8_t*>(a.gameId.data()), idLen);
      copyBytes(p + 1 + idLen, a.pkgHash.data(), PKG_HASH_BYTES);
      p[9 + idLen] = a.manifestApi;
      p[10 + idLen] = a.hostApi;
      put32(p + 11 + idLen, a.surface);
      p[15 + idLen] = a.seatsMax;
      p[16 + idLen] = a.seatsTaken;
      p[17 + idLen] = static_cast<uint8_t>(a.hostLabel.size());
      copyBytes(p + ADVERT_FIXED_BYTES + idLen, a.hostLabel.data(), a.hostLabel.size());
      break;
    }
    case Type::Join:
      copyBytes(p, frame.join.pkgHash.data(), PKG_HASH_BYTES);
      p[8] = frame.join.hostApi;
      put32(p + 9, frame.join.surface);
      break;
    case Type::Accept:
      p[0] = frame.accept.seat;
      p[1] = frame.accept.n;
      break;
    case Type::State: {
      const State& s = frame.state;
      put16(p, s.ver);
      p[2] = s.status.turn;
      p[3] = s.status.over ? 1 : 0;
      put16(p + 4, s.status.winners);
      copyBytes(p + STATE_FIXED_BYTES, s.snapshot.data(), s.snapshot.size());
      break;
    }
    case Type::Abort:
      p[0] = static_cast<uint8_t>(frame.abortReason);
      break;
    case Type::Ping:
    case Type::Ack:
      break;
  }
  return total;
}

DecodeStatus decode(const std::span<const uint8_t> in, const uint32_t session, Frame& out) {
  if (in.size() < HEADER_BYTES) return DecodeStatus::TooShort;
  if (in.size() > MAX_FRAME_BYTES) return DecodeStatus::TooLong;
  const uint8_t* const b = in.data();
  if (b[0] != MAGIC_0 || b[1] != MAGIC_1) return DecodeStatus::BadMagic;
  if (b[2] != PROTO) return DecodeStatus::BadProto;
  const uint32_t wireSession = get32(b + 4);
  if (wireSession == 0 || (session != ANY_SESSION && session != wireSession)) return DecodeStatus::BadSession;
  if (!validType(b[3])) return DecodeStatus::BadType;

  Frame f;
  f.header.type = static_cast<Type>(b[3]);
  f.header.session = wireSession;
  f.header.seq = get16(b + 8);
  f.header.ack = get16(b + 10);
  const DecodeStatus status = decodePayload(b + HEADER_BYTES, in.size() - HEADER_BYTES, f);
  if (status != DecodeStatus::Ok) return status;
  out = f;
  return DecodeStatus::Ok;
}

}  // namespace GameCore::Protocol
