#pragma once

#include <Logging.h>
#include <Protocol.h>
#include <Session.h>

#include <cstdint>

// The LINK lines of a nearby match (R13): the text of each, one function each, so a test pins it and the code that
// logs an event cannot spell it twice. Every line names the `session` id, the 8-digit lower-case hex id of the host's
// session (00000000 before a guest has joined); a STATE line names its `ver`. One line per event, never duplicated; the
// four ReliableLink counters (resend, silence, abort sent, link refused) are one line per step in which the counter
// moved, naming how far (`n`) and the running total.
//
// Each function logs through LOG_INF / LOG_ERR with the origin "LINK". The `[[maybe_unused]]` parameters are for a
// build with logging off, where the macros expand to nothing.
namespace GameLinkLog {

inline const char* abortReasonName(const GameCore::Protocol::AbortReason reason) {
  using GameCore::Protocol::AbortReason;
  switch (reason) {
    case AbortReason::Left:
      return "left";
    case AbortReason::ScriptError:
      return "script_error";
    case AbortReason::Timeout:
      return "timeout";
    case AbortReason::Full:
      return "full";
    case AbortReason::VersionMismatch:
      return "version_mismatch";
  }
  return "unknown";
}

inline const char* adoptionName(const GameCore::Adoption result) {
  using GameCore::Adoption;
  switch (result) {
    case Adoption::Adopted:
      return "Adopted";
    case Adoption::RoundStarted:
      return "RoundStarted";
    case Adoption::NotPeer:
      return "NotPeer";
    case Adoption::Busy:
      return "Busy";
    case Adoption::BadSnapshot:
      return "BadSnapshot";
    case Adoption::Stale:
      return "Stale";
    case Adoption::OutOfTurn:
      return "OutOfTurn";
    case Adoption::BadStatus:
      return "BadStatus";
  }
  return "unknown";
}

// GameLink task: every peer's send of the staged STATE returned true.
inline void stateSent([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint16_t ver) {
  LOG_INF("LINK", "session=%08x state sent ver=%u", static_cast<unsigned>(session), static_cast<unsigned>(ver));
}
// GameLink task (the sink gate): a peer's STATE went to the queue.
inline void stateReceived([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint16_t ver,
                          [[maybe_unused]] const uint8_t seat) {
  LOG_INF("LINK", "session=%08x state recv ver=%u seat=%u", static_cast<unsigned>(session), static_cast<unsigned>(ver),
          static_cast<unsigned>(seat));
}
// VM task (adoptNext): the verdict of one adopt().
inline void stateAdopted([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint16_t ver,
                         [[maybe_unused]] const uint8_t seat, [[maybe_unused]] const GameCore::Adoption result) {
  LOG_INF("LINK", "session=%08x state adopt ver=%u seat=%u result=%s", static_cast<unsigned>(session),
          static_cast<unsigned>(ver), static_cast<unsigned>(seat), adoptionName(result));
}
// GameLink task (the sink gate): the queue had no room for a STATE, left unacked; `overflows` is the running count.
inline void queueFull([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint16_t ver,
                      [[maybe_unused]] const uint32_t overflows) {
  LOG_INF("LINK", "session=%08x queue full ver=%u overflows=%u", static_cast<unsigned>(session),
          static_cast<unsigned>(ver), static_cast<unsigned>(overflows));
}
// GameLink task (the sink gate): an ABORT from a peer.
inline void abortReceived([[maybe_unused]] const uint32_t session,
                          [[maybe_unused]] const GameCore::Protocol::AbortReason reason,
                          [[maybe_unused]] const uint8_t seat) {
  LOG_INF("LINK", "session=%08x abort recv reason=%s seat=%u", static_cast<unsigned>(session), abortReasonName(reason),
          static_cast<unsigned>(seat));
}
// VM task (leave): the goodbye was sent.
inline void leaveSent([[maybe_unused]] const uint32_t session,
                      [[maybe_unused]] const GameCore::Protocol::AbortReason reason,
                      [[maybe_unused]] const uint32_t waitedMs, [[maybe_unused]] const uint32_t unacked,
                      [[maybe_unused]] const uint32_t copies) {
  LOG_INF("LINK", "session=%08x leave reason=%s waited=%ums unacked=%u copies=%u", static_cast<unsigned>(session),
          abortReasonName(reason), static_cast<unsigned>(waitedMs), static_cast<unsigned>(unacked),
          static_cast<unsigned>(copies));
}
// The stats reporter: a counter of ReliableLink::Stats moved by `n` to `total`.
inline void resent([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint32_t n,
                   [[maybe_unused]] const uint32_t total) {
  LOG_INF("LINK", "session=%08x resend n=%u total=%u", static_cast<unsigned>(session), static_cast<unsigned>(n),
          static_cast<unsigned>(total));
}
inline void silent([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint32_t n,
                   [[maybe_unused]] const uint32_t total) {
  LOG_INF("LINK", "session=%08x silence n=%u total=%u", static_cast<unsigned>(session), static_cast<unsigned>(n),
          static_cast<unsigned>(total));
}
inline void abortSent([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint32_t n,
                      [[maybe_unused]] const uint32_t total) {
  LOG_INF("LINK", "session=%08x abort sent n=%u total=%u", static_cast<unsigned>(session), static_cast<unsigned>(n),
          static_cast<unsigned>(total));
}
// A send the link adapter refused (ILink::send false), which the adapter itself does not count.
inline void linkRefused([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint32_t n,
                        [[maybe_unused]] const uint32_t total) {
  LOG_INF("LINK", "session=%08x link refused n=%u total=%u", static_cast<unsigned>(session), static_cast<unsigned>(n),
          static_cast<unsigned>(total));
}
inline void taskStarted([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint32_t stackBytes) {
  LOG_INF("LINK", "session=%08x task started; stack %u bytes", static_cast<unsigned>(session),
          static_cast<unsigned>(stackBytes));
}
inline void taskStopped([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint32_t highWaterBytes) {
  LOG_INF("LINK", "session=%08x task stopped; stack high-water %u bytes free", static_cast<unsigned>(session),
          static_cast<unsigned>(highWaterBytes));
}
// VM task (ship): a snapshot the link cannot carry (empty or over the snapshot limit), once per session.
inline void shipRefused([[maybe_unused]] const uint32_t session, [[maybe_unused]] const uint32_t bytes) {
  LOG_ERR("LINK", "session=%08x ship refused: snapshot of %u bytes", static_cast<unsigned>(session),
          static_cast<unsigned>(bytes));
}
// The factory could not bring the session up (`what` names the step); the link was ended.
inline void bringUpFailed([[maybe_unused]] const uint32_t session, [[maybe_unused]] const char* what) {
  LOG_ERR("LINK", "session=%08x bring-up failed: %s", static_cast<unsigned>(session), what);
}
// The destructor could not stop the task: the state it touches is leaked rather than freed.
inline void taskLeaked([[maybe_unused]] const uint32_t session) {
  LOG_ERR("LINK", "session=%08x task did not stop; its state is leaked", static_cast<unsigned>(session));
}

}  // namespace GameLinkLog
