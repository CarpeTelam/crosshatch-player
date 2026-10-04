#pragma once

#include <cstdint>

class MatchStore;
class SnapshotMailbox;

// What GameMatchActivity writes to and removes from the card for one match, and when: resume.bin (the latest committed
// snapshot, and the delete Over could not finish) and ch.store, with the forced exit's deadline over those SD steps.
// Loop task only. The activity owns it, binds it once to its MatchStore, and keeps the lifecycle (it calls
// setWritable from handle()); nothing here touches the VM or the screen.
class MatchPersistence {
 public:
  // The clock the deadline and the back-off read (millis() on the device, a fake in the host tests). Read at the point
  // each step needs it, never earlier, so the times are those of today's code.
  using Clock = uint32_t (*)();

  // The forced exit's SD steps (the resume write, the resume.bin delete retry, the
  // ch.store flush) start only within this long of the start of onExit(); a step that
  // would start later is skipped and logged. A step that starts in time is one tmp write
  // and rename, whose time is the card's. Leave has no deadline.
  static constexpr uint32_t FORCED_EXIT_DEADLINE_MS = 1500;

  // `store` outlives this object; `gameId` (a valid manifest id) is logged. Until bound, every call below does nothing
  // (sdStepAllowed allows).
  void bind(MatchStore& store, const char* gameId, Clock clock);

  // The state allows resume.bin to be written: Playing, Paused, Result, or HandOff, not yet Over or Error
  // (handle() keeps it).
  void setWritable(bool writable) { resumeWritable = writable; }
  bool writable() const { return resumeWritable; }

  // onExit() is running (a forced exit): from now, by the clock.
  void beginForcedExit();
  bool forcedExit() const { return inForcedExit; }
  // Entering Over could not delete resume.bin (the card refused): the retry runs until it can.
  bool deletePending() const { return resumeDeletePending; }

  // False, with one log line naming `what`, for an SD step of the forced exit that would
  // start past FORCED_EXIT_DEADLINE_MS; true for every step outside a forced exit.
  bool sdStepAllowed(const char* what);
  // Playing, Paused, Result, and HandOff: writes the newest snapshot `committed` holds as resume.bin (not
  // again until FLUSH_INTERVAL_MS after a failed write, Leave and the forced exit
  // included). Nothing in Over or Error, or without a .pkg.
  void flushResumeOf(SnapshotMailbox& committed);
  // Removes resume.bin again after Over's delete failed (resumeDeletePending), at most
  // every FLUSH_INTERVAL_MS from the last failed try unless `forced` (Leave, the forced exit).
  void retryResumeDelete(bool forced);
  // Entering Over: a finished round never resumes. Deletes resume.bin; when the card refuses, the delete stays pending.
  void onOver();
  // Writes a dirty ch.store now (round end, Leave, onExit; AD-17).
  void flushStore();

 private:
  MatchStore* store = nullptr;
  const char* gameId = "";
  Clock clock = nullptr;
  bool resumeWritable = false;
  // Entering Over could not delete resume.bin (the card refused): the loop retries until it
  // can, so a finished round's save does not survive one failed remove. Play again keeps it pending until the new
  // round's first snapshot is written over the file (flushResumeOf), so a Leave before that still removes the finished
  // round's save. Loop task.
  bool resumeDeletePending = false;
  uint32_t resumeDeleteTriedMs = 0;  // the clock at the last failed delete
  // onExit() is running (a forced exit), from the clock's forcedExitBeganMs.
  bool inForcedExit = false;
  uint32_t forcedExitBeganMs = 0;
};
