#pragma once

#include <GuestLobby.h>
#include <HostLobby.h>
#include <IClock.h>
#include <ILink.h>
#include <IRandom.h>
#include <Lobby.h>
#include <Manifest.h>
#include <Protocol.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "GamesHomeRow.h"
#include "activities/UiListActivity.h"
#include "games/GameHash.h"
#include "games/GameRandom.h"
#include "games/NearbyProbe.h"
#include "games/NearbySession.h"

// What the lobby takes from the radio layer (epic R8, entries 10, 11, and 16): the link, its label, the clock and
// random source, the probe reading, and the timing. The class itself is the device's radio: its members are the one
// place the device wiring lives, so entry 11 (EspNowLink and its label) and entry 16 (UdpLink under SIMULATOR) edit
// their bodies in GameLobbyActivity.cpp and nothing else of the lobby. The host harness subclasses it with a FakeLink
// (test/game_script/harness/NearbyLobbySupport.h) and installs it with GameLobbyActivity::setRadio.
class NearbyRadio {
 public:
  // A started link and the host's label (the radio layer builds it: EspNow::makeLabel), at most
  // Protocol::MAX_HOST_LABEL_BYTES bytes. `link` is null when the radio would not start.
  struct Opened {
    std::unique_ptr<GameCore::ILink> link;
    uint8_t label[GameCore::Protocol::MAX_HOST_LABEL_BYTES] = {};
    size_t labelLength = 0;
  };

  virtual ~NearbyRadio() = default;
  // Wi-Fi mode and free internal heap now (NearbyProbe::read() on the device).
  virtual NearbyProbe::Readings probe();
  // Brings the radio up: a new link each call, ended by whoever owns it. Lobby task; may wait.
  virtual Opened open();
  // The clock a session is given: it outlives the lobby (the session moves into the match), so a device
  // implementation hands out a static, stateless one. The random source is read once, when openHost draws the session
  // id (HostLobby keeps no reference to it), so the radio object itself may own it.
  virtual const GameCore::IClock& clock();
  virtual GameCore::IRandom& random();
  virtual GameCore::LobbySettings lobbySettings();
  virtual NearbySessionOptions sessionOptions();

 private:
  GameRandom deviceRandom;  // stateless (the hardware RNG); a member, so no static object needs a guard (AD-2)
};

// The Nearby lobby (epic play-nearby R8, entry 10; spine AD-11 Nearby, AD-13): what a player sees between picking
// Nearby on a game's title screen and the match. It is pushed over the title screen, headed by the game's name, and
// has five screens, each a FreeInkUI list on UiListActivity with the status above the list as text, so the selection
// is only ever on something a player can act on:
//  - Choose: Host a game, Join a game (and, after a failed start, "Nearby could not start. Try again." above them).
//  - Host: the host's label large, a state line ("Waiting for a player", "Player 2 joined", "Player 2 left"), a seats
//    line, then Start (greyed until a guest is seated and heard within LIVE_WINDOW_MS) and Cancel.
//  - Browse (a guest): the hosts advertising this game, first heard first, each with its label and "1 of 2 players";
//    a host with another package hash is greyed with "Different version" and takes no tap; Cancel last.
//  - Joined (a guest): "Waiting for the host to start" and the host's label, and Cancel.
//  - Ended (a guest): "The host left", "That game is full", or "Different version", and "Back to the list".
// Start (the host) and the host's Start seen by a guest replace this screen with the match, the session moved in
// (GameMatchActivity); Cancel, Back, Home, and sleep end the session: ABORT(left) to whoever is seated, the link ended
// (the radio off), then the title screen (or Home). onExit() does the same and takes no RenderLock.
//
// The lobby polls its NearbySession on the loop task at most every POLL_MS and redraws only when what is drawn
// changed: at once for a state a player waits on (a guest joined or left, the host left, started), and at most once
// per LIST_REFRESH_MIN_MS for a change in the list of hosts, with no timer-driven redraw, so a stable set of hosts
// costs no e-ink refresh. The session is read under its own lock and copied; RenderLock is taken only to change what
// the render task reads (the rows and lines).
//
// It never touches resume.bin: a nearby match saves nothing (AD-17).
class GameLobbyActivity final : public UiListActivity {
 public:
  // One constant with the mapping ActivityManager::goHome reads (GamesHomeRow.h).
  static constexpr const char* NAME = GamesHomeRow::GAME_LOBBY;

  // The loop task reads the session at most this often (estimate until the device run, entry 14).
  static constexpr uint32_t POLL_MS = 100;
  // A change in the list of hosts redraws at most this often (D2; an estimate: the panel's partial refresh is
  // unmeasured, entry 14).
  static constexpr uint32_t LIST_REFRESH_MIN_MS = 1000;
  // The host enables Start while the guest was heard within this long: three keepalives, inside the 10 s silence
  // limit that still ends a ghost (D3).
  static constexpr uint32_t LIVE_WINDOW_MS = 3000;
  // The rows there can be: every host the guest lobby lists, and Cancel.
  static constexpr size_t MAX_ROWS = GameCore::GuestLobby::MAX_HOSTS + 1;
  // A label and a "n of m players" line, NUL included.
  static constexpr size_t LABEL_BYTES = GameCore::Protocol::MAX_HOST_LABEL_BYTES + 1;
  static constexpr size_t LINE_BYTES = 64;

  // Installs the radio the lobby uses, in place of the device's (a host test's); null puts the device's back. Not
  // owned: it must outlive every lobby and session made while set.
  static void setRadio(NearbyRadio* radio);
  // The set radio's reading, else the device's: what the title screen decides the precondition on.
  static NearbyProbe::Readings probe();
  // What a lobby for `manifest` (package hash `pkgHash`) speaks for, on this host.
  static GameCore::LobbyGame lobbyGameFor(const GameCore::Manifest& manifest,
                                          const uint8_t (&pkgHash)[GamePkg::HASH_BYTES]);

  // `manifest`, `pkgHash`, and `settings` (the title screen's chosen values, which the match gets as ctx.settings) are
  // copied.
  GameLobbyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameCore::Manifest& manifest,
                    const uint8_t (&pkgHash)[GamePkg::HASH_BYTES], const GameCore::SettingValues& settings);

  void onEnter() override;
  // Ends the session when the lobby leaves any way but its own (Home, sleep). RenderLock is held by the caller and
  // never taken here.
  void onExit() override;
  // The radio is up for the whole of the lobby (spine AD-18): the device must not sleep under it.
  bool preventAutoSleep() override { return true; }

 private:
  // The screens, and what a guest's ended screen says.
  enum class Screen : uint8_t { Choose, Host, Browse, Joined, Ended };
  enum class EndKind : uint8_t { HostLeft, Full, DifferentVersion };
  // What a row does.
  enum class RowKind : uint8_t { HostGame, JoinGame, Start, Cancel, JoinHost, BackToList };
  // The host screen's state line.
  enum class HostBody : uint8_t { Waiting, Joined, Left };

  // A listed host, copied out of GuestLobby under the session's lock.
  struct ListedHost {
    GameCore::PeerHandle handle;
    uint32_t session = 0;
    uint8_t labelLength = 0;
    uint8_t label[GameCore::Protocol::MAX_HOST_LABEL_BYTES] = {};
    uint8_t seatsTaken = 0;
    uint8_t seatsMax = 0;
    bool different = false;
    bool operator==(const ListedHost& other) const;
  };
  // What the guest lobby holds, as the poll reads it.
  struct GuestView {
    GameCore::GuestState state = GameCore::GuestState::Browsing;
    GameCore::Refusal refusal = GameCore::Refusal::None;
    uint8_t hostCount = 0;
    ListedHost hosts[GameCore::GuestLobby::MAX_HOSTS];
    uint8_t joinedLabelLength = 0;
    uint8_t joinedLabel[GameCore::Protocol::MAX_HOST_LABEL_BYTES] = {};
    bool sameHosts(const GuestView& other) const;
  };
  // What the host lobby holds, with the state line the events made.
  struct HostView {
    HostBody body = HostBody::Waiting;
    uint8_t bodySeat = 0;
    uint8_t seats = 1;
    uint8_t limit = 2;
    bool startEnabled = false;
    bool operator==(const HostView& other) const;
  };

  int listCount() const override { return static_cast<int>(rowCount); }
  const char* headerTitle() const override { return manifest.name; }
  void buildScreen(UiScreen& screen) override;
  void drawFooter() override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;

  // ---- opening and ending the session (loop task) ----
  void openHost();
  void openGuest();
  // Choose again with the "could not start" notice; the session, if any, is ended first.
  void failToChoose();
  // ABORT(left) to whoever is seated and the session destroyed (the link ended, the radio off). Idempotent.
  void teardown();
  // Cancel and Back: ends the session, then the lobby.
  void leave();

  // ---- the poll (loop task) ----
  void pollHost(uint64_t now);
  // True when the pass is over because the lobby handed over to the match. `forceList` redraws a changed list of hosts
  // without waiting for LIST_REFRESH_MIN_MS (a tap or Back to the list just made it stale).
  bool pollGuest(uint64_t now, bool forceList);
  void readHost(uint64_t now);
  void readGuest();
  void startHostMatch();
  void startGuestMatch();
  // Replaces this lobby with the match, the session moved in; the notice and Choose when that cannot be.
  void launchMatch();
  void joinHostRow(size_t row);
  void backToList();

  // ---- what the render task reads (RenderLock held by the caller) ----
  void showScreen(Screen next, size_t selected);
  void showHost();
  void buildRows();
  void fillHostRows();
  void drawStatus(UiScreen& screen) const;
  static void copyLabel(char (&out)[LABEL_BYTES], const uint8_t* bytes, size_t length);

  GameCore::Manifest manifest;
  uint8_t pkgHash[GamePkg::HASH_BYTES] = {};
  GameCore::SettingValues settings;
  // The device's radio when none is set; `radio` is the one in use.
  NearbyRadio deviceRadio;
  NearbyRadio* radio;
  // Loop task only. Null until Host or Join, and again once it is torn down or handed to the match.
  std::unique_ptr<NearbySession> session;

  // ---- polled state (loop task only) ----
  uint64_t lastPollMs = 0;
  uint64_t lastListMs = 0;
  bool polled = false;
  HostView hostNow;
  GuestView guestNow;

  // ---- drawn state (written under RenderLock, read by the render task) ----
  Screen current = Screen::Choose;
  EndKind endKind = EndKind::HostLeft;
  bool couldNotStart = false;
  HostView hostShown;
  GuestView guestShown;
  // The host's own label (Host screen), or the joined host's (Joined screen).
  char labelText[LABEL_BYTES] = {};
  char statusLine[LINE_BYTES] = {};
  char smallLine[LINE_BYTES] = {};
  RowKind rowKind[MAX_ROWS] = {};
  freeink::ui::ListItem rowItems[MAX_ROWS] = {};
  size_t rowCount = 0;
  // Which build of the rows the render task last drew. The loop task rebuilds the rows (buildRows: rowSerial counts
  // them, under RenderLock) before the render task builds the screen from them (buildScreen sets builtSerial), so a tap
  // can be routed against the table of the build before while rowKind and the host rows are already the next one's. A
  // tap that finds the two apart is dropped (activateIndex): it names a row that is no longer there, and joining the
  // host now at that index would join one the player did not touch.
  uint32_t rowSerial = 0;
  std::atomic<uint32_t> builtSerial{0};
  // A listed host's row text, and the (handle, session) the row was drawn for: a tap joins that and nothing newer.
  char rowLabel[GameCore::GuestLobby::MAX_HOSTS][LABEL_BYTES] = {};
  char rowValue[GameCore::GuestLobby::MAX_HOSTS][LINE_BYTES] = {};
  GameCore::PeerHandle rowHandle[GameCore::GuestLobby::MAX_HOSTS];
  uint32_t rowSession[GameCore::GuestLobby::MAX_HOSTS] = {};
};
