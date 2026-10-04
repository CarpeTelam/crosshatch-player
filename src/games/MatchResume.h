#pragma once

#include <HostCaps.h>
#include <Manifest.h>
#include <Roster.h>

#include <cstdint>
#include <span>

#include "GameSaveStore.h"

// What Start::Resume found on the card before the VM is created (GameMatchActivity::onEnter).
struct ResumeSeed {
  // New: no usable save, the match starts new with the caller's roster (logged: nothing is lost). Resume: `snapshot`,
  // `version`, and `roster` are the save's. Refused: a save is there and cannot be used now, `refusal` says why, and
  // the caller shows the error view and leaves the file, which a new match would replace.
  enum class Outcome : uint8_t { New, Resume, Refused };
  enum class Refusal : uint8_t { CannotRead, NotHere };

  Outcome outcome = Outcome::New;
  Refusal refusal = Refusal::CannotRead;
  // In the store's buffer; valid until the store's next call. Empty unless Resume.
  std::span<const uint8_t> snapshot;
  uint16_t version = 0;
  // The save's roster; default-constructed (solo) unless Resume. The caller makes it the match's (roster, lifecycle)
  // before any event is applied.
  GameCore::Roster roster;
};

// Start::Resume, after the assets load and before the VM is created: reads the save through `saves` (loadResume) and,
// when it gives none, asks the card again why (peekResume, which reads through the store's buffer and leaves its
// roster). `manifest` and `host` decide what this host can start.
ResumeSeed seedResume(GameSaveStore& saves, const GameCore::Manifest& manifest, const GameCore::HostCaps& host);
