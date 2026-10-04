#if FREEINK_CAP_GAMES

#include "MatchResume.h"

#include <Logging.h>

ResumeSeed seedResume(GameSaveStore& saves, const GameCore::Manifest& manifest, const GameCore::HostCaps& host) {
  ResumeSeed seed;
  bool unreadable = false;
  GameCore::Roster saved;
  seed.snapshot = saves.loadResume(seed.version, unreadable, manifest, host, saved);
  if (seed.snapshot.empty()) {
    seed.refusal = ResumeSeed::Refusal::CannotRead;
    if (unreadable) {
      // A save is there and would not read, a fault that may pass (the title screen offered Continue for it).
      LOG_ERR("GAME", "%s: resume.bin could not be read; not starting a new match over it", manifest.id);
      seed.outcome = ResumeSeed::Outcome::Refused;
      return seed;
    }
    // Why there is none, asked of the card again through the store's buffer (nothing references it after an empty
    // load) and never its roster, so the error view writes nothing and a new match writes its own roster.
    switch (saves.peekResume(manifest, host)) {
      case GameSaveStore::SaveState::Unstartable:
        // A save of this package whose mode or seats this host cannot start, or a later firmware's (an unknown mode
        // byte, a newer file or codec version, or an oversized snapshot): a new match here would replace it with its
        // first snapshot.
        LOG_ERR("GAME", "%s: resume.bin is a save this host cannot start; not starting a new match over it",
                manifest.id);
        seed.refusal = ResumeSeed::Refusal::NotHere;
        seed.outcome = ResumeSeed::Outcome::Refused;
        return seed;
      case GameSaveStore::SaveState::Unreadable:
      case GameSaveStore::SaveState::Valid:
        // A read that faults now (the first did not) cannot rule out a save, and a save that reads now changed since
        // the load refused it: either way the file is kept.
        LOG_ERR("GAME", "%s: resume.bin could not be checked again; not starting a new match over it", manifest.id);
        seed.outcome = ResumeSeed::Outcome::Refused;
        return seed;
      case GameSaveStore::SaveState::None:
        break;
    }
    // No file, or one that was read and is no save (logged with its reason): nothing to lose.
    LOG_INF("GAME", "%s: no usable resume.bin; starting a new match", manifest.id);
    seed.outcome = ResumeSeed::Outcome::New;
    return seed;
  }
  // The save's roster is the match's, whatever the caller passed: the VM's Session, the lifecycle, and the store's
  // writes (loadResume adopted it) all follow it. The caller assigns the roster and the lifecycle.
  seed.outcome = ResumeSeed::Outcome::Resume;
  seed.roster = saved;
  LOG_INF("GAME", "%s: resuming the save's roster: %s, %u seat(s)", manifest.id, GameCore::modeName(saved.mode),
          static_cast<unsigned>(saved.seats));
  return seed;
}

#endif  // FREEINK_CAP_GAMES
