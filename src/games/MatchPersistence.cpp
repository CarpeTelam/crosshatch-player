#if FREEINK_CAP_GAMES

#include "MatchPersistence.h"

#include <Logging.h>

#include "MatchStore.h"
#include "SnapshotMailbox.h"

void MatchPersistence::bind(MatchStore& matchStore, const char* id, const Clock now) {
  store = &matchStore;
  gameId = id;
  clock = now;
}

void MatchPersistence::beginForcedExit() {
  if (clock) forcedExitBeganMs = clock();
  inForcedExit = true;
}

bool MatchPersistence::sdStepAllowed(const char* what) {
  if (!inForcedExit || !clock || clock() - forcedExitBeganMs < FORCED_EXIT_DEADLINE_MS) return true;
  LOG_ERR("GAME", "%s: forced exit past %u ms; skipped %s", gameId, static_cast<unsigned>(FORCED_EXIT_DEADLINE_MS),
          what);
  return false;
}

void MatchPersistence::retryResumeDelete(const bool forced) {
  if (!store || !clock || !resumeDeletePending || !store->ready()) return;
  const uint32_t now = clock();
  if (!forced && now - resumeDeleteTriedMs < GameSaveStore::FLUSH_INTERVAL_MS) return;
  if (!sdStepAllowed("the resume.bin delete")) return;
  if (store->saves().deleteResume()) {
    resumeDeletePending = false;
  } else {
    resumeDeleteTriedMs = now;
  }
}

void MatchPersistence::onOver() {
  if (!store || !clock) return;
  // A finished round never resumes. A snapshot with `over` set is never written, and
  // one still pending is dropped by the next flushResume.
  resumeDeletePending = !store->saves().deleteResume();
  resumeDeleteTriedMs = clock();
}

void MatchPersistence::flushResumeOf(SnapshotMailbox& committed) {
  if (!store || !clock || !resumeWritable || !store->ready() || !committed.pending()) return;
  if (!sdStepAllowed("the resume write")) return;
  const uint32_t replacedBefore = store->saves().resumeReplacements();
  store->saves().flushResume(committed, clock());
  // The finished round's resume.bin is gone once a snapshot has replaced it, or once a write that then failed to
  // rename has removed it (the new snapshot waits in resume.bin.tmp). Either way an Over delete that failed has
  // nothing left to remove, and a retry from here on would delete this round's save. Until then the delete keeps
  // retrying (loopPlaying, loopView, Leave, the forced exit).
  if (store->saves().resumeReplacements() != replacedBefore) resumeDeletePending = false;
}

void MatchPersistence::flushStore() {
  if (!store || !clock || !store->ready()) return;  // the match never got that far
  if (!store->slot().dirty() || !sdStepAllowed("the ch.store flush")) return;
  // Safe with a leaked task too: the slot's mutex is held only for a copy inside
  // a locked binding, which an abandon never deletes nor leaves suspended, so the
  // flush waits at most one copy, and it saves every set until the leak.
  store->flush(clock());
}

#endif  // FREEINK_CAP_GAMES
