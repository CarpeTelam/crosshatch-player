#if FREEINK_CAP_GAMES

#include "GameLobbyActivity.h"

#include <ApiLevel.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>

#include "GameMatchActivity.h"
#include "GameSplashLayout.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "games/GameClock.h"
#include "games/GameHostCaps.h"
#include "games/GameRandom.h"

namespace fui = freeink::ui;

namespace {

using GameCore::GuestState;
using GameCore::HostEventKind;

static_assert(GamePkg::HASH_BYTES == GameCore::Protocol::PKG_HASH_BYTES,
              "the lobby advertises the package hash the registry read");

// The live window is three keepalives and still inside the silence limit that ends a ghost seat (D3): a change to the
// link's defaults must be looked at here.
static_assert(GameLobbyActivity::LIVE_WINDOW_MS >= 3 * GameCore::ReliableLink::DEFAULT_KEEPALIVE_MS &&
                  GameLobbyActivity::LIVE_WINDOW_MS < GameCore::ReliableLink::DEFAULT_SILENCE_MS,
              "Start's live window sits between three keepalives and the silence limit");

// The radio a host test installed (GameLobbyActivity::setRadio), else null: the one static of the lobby.
NearbyRadio* radioOverride = nullptr;

// One centred line of the status block above the list, wrapped to `style.maxLines`, then `gap`.
void textLine(UiAppHost::UiScreen& screen, const char* text, fui::TextStyle style, const int16_t gap) {
  style.align = fui::TextAlign::Center;
  const int16_t height = fui::measureWrappedText(screen.target(), text, style, screen.body().width).height;
  screen.target().text(screen.takeTop(height, gap), text, style);
}

}  // namespace

// ---- the device's radio (entries 11 and 16 edit these) ----

NearbyProbe::Readings NearbyRadio::probe() {
#if defined(SIMULATOR)
  // NearbyProbe.cpp is device only and the simulator has no radio until entry 16: Wi-Fi is never in use and the heap is
  // never short.
  return NearbyProbe::Readings{false, SIZE_MAX};
#else
  return NearbyProbe::read();
#endif
}

NearbyRadio::Opened NearbyRadio::open() {
  // Entry 11 brings up EspNowLink and builds its label here (EspNow::makeLabel); entry 16 chooses UdpLink under
  // SIMULATOR. Until then no radio starts, which the lobby shows as "Nearby could not start".
  return Opened{};
}

const GameCore::IClock& NearbyRadio::clock() {
  // Stateless and constant, so it outlives the lobby that hands the session to the match, with no guard variable and
  // no static RAM.
  static constexpr GameClock CLOCK{};
  return CLOCK;
}

GameCore::IRandom& NearbyRadio::random() { return deviceRandom; }

GameCore::LobbySettings NearbyRadio::lobbySettings() { return GameCore::LobbySettings{}; }

NearbySessionOptions NearbyRadio::sessionOptions() { return NearbySessionOptions{}; }

// ---- statics ----

void GameLobbyActivity::setRadio(NearbyRadio* radio) { radioOverride = radio; }

NearbyProbe::Readings GameLobbyActivity::probe() {
  if (radioOverride) return radioOverride->probe();
  NearbyRadio device;
  return device.probe();
}

GameCore::LobbyGame GameLobbyActivity::lobbyGameFor(const GameCore::Manifest& manifest,
                                                    const uint8_t (&pkgHash)[GamePkg::HASH_BYTES]) {
  GameCore::LobbyGame game;
  game.setGameId(manifest.id);
  std::copy_n(pkgHash, GamePkg::HASH_BYTES, game.pkgHash.begin());
  game.manifestApi = static_cast<uint8_t>(manifest.api);
  game.seatsMin = manifest.seatsMin;
  game.seatsMax = manifest.seatsMax;
  game.surface = API_SURFACE_CRC;
  game.apiPreview = !API_LEVEL_FROZEN;
  game.caps = gameHostCaps();
  return game;
}

void GameLobbyActivity::copyLabel(char (&out)[LABEL_BYTES], const uint8_t* bytes, const size_t length) {
  const size_t count = std::min(length, LABEL_BYTES - 1);
  for (size_t i = 0; i < count; ++i) {
    // The label is a stranger's bytes: a control character would be drawn as a glyph.
    out[i] = bytes[i] < 0x20 || bytes[i] == 0x7F ? '?' : static_cast<char>(bytes[i]);
  }
  out[count] = '\0';
}

bool GameLobbyActivity::ListedHost::operator==(const ListedHost& other) const {
  return handle == other.handle && session == other.session && labelLength == other.labelLength &&
         std::memcmp(label, other.label, labelLength) == 0 && seatsTaken == other.seatsTaken &&
         seatsMax == other.seatsMax && different == other.different;
}

bool GameLobbyActivity::GuestView::sameHosts(const GuestView& other) const {
  if (hostCount != other.hostCount) return false;
  for (size_t i = 0; i < hostCount; ++i) {
    if (!(hosts[i] == other.hosts[i])) return false;
  }
  return true;
}

bool GameLobbyActivity::HostView::operator==(const HostView& other) const {
  return body == other.body && bodySeat == other.bodySeat && seats == other.seats && limit == other.limit &&
         startEnabled == other.startEnabled;
}

// ---- the activity ----

GameLobbyActivity::GameLobbyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const GameCore::Manifest& manifest, const uint8_t (&hash)[GamePkg::HASH_BYTES],
                                     const GameCore::SettingValues& settings)
    : UiListActivity(NAME, renderer, mappedInput),
      manifest(manifest),
      settings(settings),
      radio(radioOverride ? radioOverride : &deviceRadio) {
  std::memcpy(pkgHash, hash, sizeof(pkgHash));
}

void GameLobbyActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  showScreen(Screen::Choose, 0);
}

void GameLobbyActivity::onExit() {
  // ActivityManager holds the render mutex here (a Replace exits each stacked screen): take no RenderLock. The session
  // is only ever touched by the loop task, which is the task running this, and the GameLink task never takes the lock.
  // Null already after Cancel, Back, or the hand-off to the match, so a second end sends and ends nothing.
  teardown();
  UiListActivity::onExit();
}

void GameLobbyActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---- drawing ----

void GameLobbyActivity::buildScreen(UiScreen& ui) {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints, as on a plain list screen.
  ui.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(GameSplashLayout::bandTop(renderer)),
                                            static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                            static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                            static_cast<int16_t>(safe.x)});
  ui.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().verticalSpacing));
  builtSerial.store(rowSerial);  // the rows this table is built from
  drawStatus(ui);
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = ui.theme().smallText;
  props.subtitleText.maxLines = 1;
  syncListViewport(ui, props);
  ui.list(props);
}

void GameLobbyActivity::drawStatus(UiScreen& ui) const {
  const fui::ThemeTokens& theme = ui.theme();
  const int16_t gap = theme.spaceSm;
  fui::TextStyle body = theme.bodyText;
  body.maxLines = 2;
  fui::TextStyle small = theme.smallText;
  switch (current) {
    case Screen::Choose:
      if (couldNotStart) textLine(ui, tr(STR_GAMES_NEARBY_COULD_NOT_START), body, gap);
      break;
    case Screen::Host:
      textLine(ui, labelText, theme.titleText, gap);
      textLine(ui, statusLine, body, gap);
      textLine(ui, smallLine, small, gap);
      break;
    case Screen::Browse:
      if (guestShown.hostCount == 0) textLine(ui, tr(STR_GAMES_NEARBY_SEARCHING), body, gap);
      break;
    case Screen::Joined:
      textLine(ui, tr(STR_GAMES_NEARBY_WAITING_FOR_START), body, gap);
      textLine(ui, labelText, small, gap);
      break;
    case Screen::Ended:
      textLine(ui, statusLine, body, gap);
      break;
  }
}

void GameLobbyActivity::showScreen(const Screen next, const size_t selected) {
  current = next;
  buildRows();
  activeNav().reset(static_cast<int>(std::min(selected, rowCount - 1)));
  requestUpdate();
}

void GameLobbyActivity::buildRows() {
  rowCount = 0;
  ++rowSerial;
  const auto add = [this](const RowKind kind, const char* label, const bool enabled = true) {
    fui::ListItem item;
    item.label = label;
    item.enabled = enabled;
    item.actionValue = static_cast<int16_t>(rowCount);
    rowKind[rowCount] = kind;
    rowItems[rowCount] = item;
    ++rowCount;
  };
  switch (current) {
    case Screen::Choose:
      add(RowKind::HostGame, tr(STR_GAMES_NEARBY_HOST));
      add(RowKind::JoinGame, tr(STR_GAMES_NEARBY_JOIN));
      break;
    case Screen::Host:
      add(RowKind::Start, tr(STR_GAMES_NEARBY_START), hostShown.startEnabled);
      add(RowKind::Cancel, tr(STR_CANCEL));
      break;
    case Screen::Browse:
      fillHostRows();
      add(RowKind::Cancel, tr(STR_CANCEL));
      break;
    case Screen::Joined:
      add(RowKind::Cancel, tr(STR_CANCEL));
      break;
    case Screen::Ended:
      add(RowKind::BackToList, tr(STR_GAMES_NEARBY_BACK_TO_LIST));
      break;
  }
}

void GameLobbyActivity::fillHostRows() {
  for (size_t i = 0; i < guestShown.hostCount; ++i) {
    const ListedHost& host = guestShown.hosts[i];
    copyLabel(rowLabel[i], host.label, host.labelLength);
    snprintf(rowValue[i], sizeof(rowValue[i]), tr(STR_GAMES_NEARBY_SEATS), static_cast<unsigned>(host.seatsTaken),
             static_cast<unsigned>(host.seatsMax));
    rowHandle[i] = host.handle;
    rowSession[i] = host.session;
    fui::ListItem item;
    item.label = rowLabel[i];
    item.value = rowValue[i];
    // A host with another package hash is listed, greyed, and takes no tap.
    item.subtitle = host.different ? tr(STR_GAMES_NEARBY_DIFFERENT_VERSION) : nullptr;
    item.enabled = !host.different;
    item.actionValue = static_cast<int16_t>(rowCount);
    rowKind[rowCount] = RowKind::JoinHost;
    rowItems[rowCount] = item;
    ++rowCount;
  }
}

void GameLobbyActivity::showHost() {
  switch (hostShown.body) {
    case HostBody::Waiting:
      snprintf(statusLine, sizeof(statusLine), "%s", tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER));
      snprintf(smallLine, sizeof(smallLine), tr(STR_GAMES_NEARBY_SEATS), static_cast<unsigned>(hostShown.seats),
               static_cast<unsigned>(hostShown.limit));
      break;
    case HostBody::Joined:
      snprintf(statusLine, sizeof(statusLine), tr(STR_GAMES_NEARBY_PLAYER_JOINED),
               static_cast<unsigned>(hostShown.bodySeat));
      snprintf(smallLine, sizeof(smallLine), tr(STR_GAMES_NEARBY_SEATS), static_cast<unsigned>(hostShown.seats),
               static_cast<unsigned>(hostShown.limit));
      break;
    case HostBody::Left:
      snprintf(statusLine, sizeof(statusLine), tr(STR_GAMES_NEARBY_PLAYER_LEFT),
               static_cast<unsigned>(hostShown.bodySeat));
      snprintf(smallLine, sizeof(smallLine), "%s", tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER));
      break;
  }
}

// ---- input ----

void GameLobbyActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= rowCount) return;
  if (builtSerial.load() != rowSerial) {
    // The rows changed since the table this tap (or the selection the player saw) came from was built: do nothing, and
    // draw the rows the player can now tap.
    LOG_DBG("GAME", "Nearby: a tap on rows that were rebuilt since they were drawn is dropped");
    requestUpdate();
    return;
  }
  switch (rowKind[index]) {
    case RowKind::HostGame:
      openHost();
      break;
    case RowKind::JoinGame:
      openGuest();
      break;
    case RowKind::Start:
      // A greyed Start still takes a Confirm on the selection: nothing starts until a guest is seated and heard.
      if (hostShown.startEnabled) startHostMatch();
      break;
    case RowKind::Cancel:
      leave();
      break;
    case RowKind::JoinHost:
      joinHostRow(static_cast<size_t>(index));
      break;
    case RowKind::BackToList:
      backToList();
      break;
  }
}

void GameLobbyActivity::onBackButton() { leave(); }

void GameLobbyActivity::leave() {
  app.clearTapFlash();
  teardown();
  // Pops to the title screen that pushed this lobby, as it was.
  finish();
}

void GameLobbyActivity::teardown() {
  if (!session) return;
  if (session->matchBegun()) {
    // Started and not yet handed over (a match that could not be allocated): the match phase's goodbye.
    session->leave(GameCore::Protocol::AbortReason::Left, 0);
  } else {
    // ABORT(left) to whoever is seated; nothing for a lobby with no one in it.
    auto lobby = session->lobby();
    if (lobby.host()) {
      lobby.host()->cancel();
    } else if (lobby.guest()) {
      lobby.guest()->cancel();
    }
  }
  session.reset();  // joins the task and ends the link: the radio goes off
}

bool GameLobbyActivity::handleCustomInput() {
  if (!session) return false;
  const uint64_t now = radio->clock().nowMs();
  if (polled && !GameCore::elapsed(now, lastPollMs, POLL_MS)) return false;
  polled = true;
  lastPollMs = now;
  if (session->role() == NearbySession::Role::Host) {
    pollHost(now);
    return false;  // the host leaves for the match on its Start, never in a poll
  }
  return pollGuest(now, false);
}

// ---- opening ----

void GameLobbyActivity::openHost() {
  app.clearTapFlash();
  NearbyRadio::Opened opened = radio->open();
  if (!opened.link) {
    LOG_ERR("GAME", "Nearby: the radio did not start for %s", manifest.id);
    failToChoose();
    return;
  }
  // A radio that reports more label bytes than its buffer holds must not advertise what lies beyond it.
  opened.labelLength = std::min(opened.labelLength, sizeof(opened.label));
  const GameCore::LobbyGame game = lobbyGameFor(manifest, pkgHash);
  session = NearbySession::openHost(std::move(opened.link), radio->clock(), radio->random(), game,
                                    std::span<const uint8_t>(opened.label, opened.labelLength), radio->lobbySettings(),
                                    radio->sessionOptions());
  if (!session) {
    LOG_ERR("GAME", "Nearby: no host session for %s", manifest.id);
    failToChoose();
    return;
  }
  LOG_INF("GAME", "Nearby: hosting %s", manifest.id);
  hostNow = HostView{};
  polled = false;
  readHost(radio->clock().nowMs());
  RenderLock lock(*this);
  couldNotStart = false;
  copyLabel(labelText, opened.label, opened.labelLength);
  hostShown = hostNow;
  showHost();
  // Start is greyed until a guest joins: the selection starts on Cancel.
  showScreen(Screen::Host, hostShown.startEnabled ? 0 : 1);
}

void GameLobbyActivity::openGuest() {
  app.clearTapFlash();
  NearbyRadio::Opened opened = radio->open();
  if (!opened.link) {
    LOG_ERR("GAME", "Nearby: the radio did not start for %s", manifest.id);
    failToChoose();
    return;
  }
  const GameCore::LobbyGame game = lobbyGameFor(manifest, pkgHash);
  session = NearbySession::openGuest(std::move(opened.link), radio->clock(), game, radio->lobbySettings(),
                                     radio->sessionOptions());
  if (!session) {
    LOG_ERR("GAME", "Nearby: no guest session for %s", manifest.id);
    failToChoose();
    return;
  }
  LOG_INF("GAME", "Nearby: looking for %s", manifest.id);
  guestNow = GuestView{};
  polled = false;
  readGuest();
  RenderLock lock(*this);
  couldNotStart = false;
  guestShown = guestNow;
  lastListMs = radio->clock().nowMs();
  showScreen(Screen::Browse, 0);
}

void GameLobbyActivity::failToChoose() {
  teardown();
  RenderLock lock(*this);
  couldNotStart = true;
  showScreen(Screen::Choose, 0);
}

// ---- the poll ----

void GameLobbyActivity::readHost(const uint64_t now) {
  auto lobby = session->lobby();
  GameCore::HostLobby* host = lobby.host();
  if (!host) return;
  GameCore::HostEvent event;
  while (host->poll(event)) {
    hostNow.body = event.kind == HostEventKind::GuestJoined ? HostBody::Joined : HostBody::Left;
    hostNow.bodySeat = event.seat;
  }
  hostNow.seats = host->seatsTaken();
  hostNow.limit = host->seatLimit();
  // The event queue keeps the newest few: a screen that fell behind resyncs from the seats.
  if (hostNow.seats >= 2 && hostNow.body != HostBody::Joined) {
    hostNow.body = HostBody::Joined;
    hostNow.bodySeat = hostNow.seats;
  } else if (hostNow.seats < 2 && hostNow.body == HostBody::Joined) {
    hostNow.body = HostBody::Left;
  }
  // Start only for a guest that was heard lately: a seat stays held until the 10 s silence limit, and Start over a
  // ghost would begin a match with nobody in it.
  bool live = true;
  for (uint8_t seat = 2; seat <= hostNow.seats; ++seat) {
    if (GameCore::elapsed(now, host->guestHeardMs(seat), LIVE_WINDOW_MS)) live = false;
  }
  hostNow.startEnabled = host->state() == GameCore::HostState::Waiting && host->canStart() && live;
}

void GameLobbyActivity::pollHost(const uint64_t now) {
  readHost(now);
  if (current == Screen::Host && hostNow == hostShown) return;
  RenderLock lock(*this);
  const bool wasEnabled = hostShown.startEnabled;
  hostShown = hostNow;
  showHost();
  buildRows();
  // A guest that joins selects Start; one that goes quiet moves the selection off the greyed row.
  if (hostShown.startEnabled && !wasEnabled) {
    activeNav().requestSelection(0);
  } else if (!hostShown.startEnabled && activeNav().selected.load() == 0) {
    activeNav().requestSelection(1);
  }
  requestUpdate();
}

void GameLobbyActivity::readGuest() {
  auto lobby = session->lobby();
  const GameCore::GuestLobby* guest = lobby.guest();
  if (!guest) return;
  guestNow.state = guest->state();
  guestNow.refusal = guest->refusal();
  const size_t count = std::min(guest->hostCount(), GameCore::GuestLobby::MAX_HOSTS);
  guestNow.hostCount = static_cast<uint8_t>(count);
  for (size_t i = 0; i < count; ++i) {
    const GameCore::HostEntry& entry = guest->host(i);
    ListedHost& host = guestNow.hosts[i];
    host.handle = entry.handle;
    host.session = entry.session;
    host.labelLength = std::min<uint8_t>(entry.labelLen, sizeof(host.label));
    std::memcpy(host.label, entry.label.data(), host.labelLength);
    host.seatsTaken = entry.seatsTaken;
    host.seatsMax = entry.seatsMax;
    host.different = entry.verdict == GameCore::HostVerdict::DifferentVersion;
  }
  const GameCore::HostEntry& joined = guest->joinedHost();
  guestNow.joinedLabelLength = std::min<uint8_t>(joined.labelLen, sizeof(guestNow.joinedLabel));
  std::memcpy(guestNow.joinedLabel, joined.label.data(), guestNow.joinedLabelLength);
}

bool GameLobbyActivity::pollGuest(const uint64_t now, const bool forceList) {
  readGuest();
  if (guestNow.state == GuestState::Started) {
    startGuestMatch();
    return true;
  }
  Screen next = Screen::Browse;
  EndKind kind = endKind;
  switch (guestNow.state) {
    case GuestState::Joining:
    case GuestState::Joined:
      next = Screen::Joined;
      break;
    case GuestState::Refused:
      next = Screen::Ended;
      kind = guestNow.refusal == GameCore::Refusal::VersionMismatch ? EndKind::DifferentVersion : EndKind::Full;
      break;
    case GuestState::HostGone:
      next = Screen::Ended;
      kind = EndKind::HostLeft;
      break;
    case GuestState::Browsing:
    case GuestState::Started:
    case GuestState::Cancelled:
      break;
  }
  const bool screenChanged = next != current || (next == Screen::Ended && kind != endKind);
  const bool hostsChanged = next == Screen::Browse && !guestNow.sameHosts(guestShown);
  if (!screenChanged && !hostsChanged) return false;
  // A change in the list of hosts waits for the refresh bound (D2); a state a player waits on does not.
  if (!screenChanged && !forceList && !GameCore::elapsed(now, lastListMs, LIST_REFRESH_MIN_MS)) return false;
  RenderLock lock(*this);
  guestShown = guestNow;
  lastListMs = now;
  if (next == Screen::Joined) copyLabel(labelText, guestShown.joinedLabel, guestShown.joinedLabelLength);
  if (next == Screen::Ended) {
    endKind = kind;
    const char* text = kind == EndKind::HostLeft ? tr(STR_GAMES_NEARBY_HOST_LEFT)
                       : kind == EndKind::Full   ? tr(STR_GAMES_NEARBY_GAME_FULL)
                                                 : tr(STR_GAMES_NEARBY_DIFFERENT_VERSION);
    snprintf(statusLine, sizeof(statusLine), "%s", text);
  }
  if (screenChanged) {
    showScreen(next, 0);
  } else {
    // The same list with other hosts in it: the selection stays on the host it was on (by handle and session, never by
    // index, or a Confirm after the redraw would join a host the player did not pick), else moves to Cancel.
    const size_t selected = static_cast<size_t>(activeNav().selected.load());
    bool onHost = false;
    GameCore::PeerHandle selectedHandle;
    uint32_t selectedSession = 0;
    if (selected < rowCount && rowKind[selected] == RowKind::JoinHost) {
      onHost = true;
      selectedHandle = rowHandle[selected];
      selectedSession = rowSession[selected];
    }
    const bool wasEmpty = rowCount == 1;  // only Cancel was listed
    buildRows();
    size_t target = wasEmpty ? 0 : rowCount - 1;  // the first host to appear, else Cancel
    if (onHost) {
      for (size_t i = 0; i < rowCount; ++i) {
        if (rowKind[i] == RowKind::JoinHost && rowHandle[i] == selectedHandle && rowSession[i] == selectedSession) {
          target = i;
          break;
        }
      }
    }
    activeNav().requestSelection(static_cast<int>(target));
    requestUpdate();
  }
  return false;
}

void GameLobbyActivity::joinHostRow(const size_t row) {
  if (!session || row >= guestShown.hostCount) return;
  if (guestShown.hosts[row].different) return;  // greyed: another version takes no tap, Confirm included
  GameCore::JoinResult result = GameCore::JoinResult::NotBrowsing;
  {
    auto lobby = session->lobby();
    if (lobby.guest()) result = lobby.guest()->join(rowHandle[row], rowSession[row]);
  }
  // A host that expired between the draw and the tap, or one that turned out to be another version, joins nothing:
  // the list is read again at once.
  if (result != GameCore::JoinResult::Sent) {
    LOG_INF("GAME", "Nearby: a join of a listed host was not sent (%u): listing again", static_cast<unsigned>(result));
  }
  app.clearTapFlash();
  pollGuest(radio->clock().nowMs(), true);
}

void GameLobbyActivity::backToList() {
  if (!session) return;
  {
    auto lobby = session->lobby();
    if (lobby.guest()) lobby.guest()->backToList();
  }
  app.clearTapFlash();
  pollGuest(radio->clock().nowMs(), true);
}

// ---- starting the match ----

void GameLobbyActivity::startHostMatch() {
  if (!session) return;
  // The enabled Start was drawn from a poll up to POLL_MS ago: read the guest's liveness again before it starts.
  const uint64_t now = radio->clock().nowMs();
  readHost(now);
  if (!hostNow.startEnabled) {
    LOG_INF("GAME", "Nearby: Start for %s found the guest quiet", manifest.id);
    pollHost(now);
    return;
  }
  switch (session->startHost()) {
    case GameCore::StartResult::Started:
      launchMatch();
      break;
    case GameCore::StartResult::NotEnoughSeats:
    case GameCore::StartResult::NotWaiting:
      // The seats changed under the tap: the events that did it are read by the next poll.
      LOG_INF("GAME", "Nearby: Start for %s found no seated guest", manifest.id);
      break;
    case GameCore::StartResult::LinkFailed:
      LOG_ERR("GAME", "Nearby: the link refused Start for %s", manifest.id);
      failToChoose();
      break;
  }
}

void GameLobbyActivity::startGuestMatch() {
  if (!session->beginMatch()) {
    LOG_ERR("GAME", "Nearby: the match of %s could not begin", manifest.id);
    failToChoose();
    return;
  }
  launchMatch();
}

void GameLobbyActivity::launchMatch() {
  const GameCore::Roster roster = session->roster();
  // The session moves in only when the activity is allocated: a failed nothrow new leaves the argument alone.
  auto match = makeUniqueNoThrow<GameMatchActivity>(renderer, mappedInput, manifest, roster,
                                                    GameMatchActivity::Start::New, settings, std::move(session));
  if (!match) {
    LOG_ERR("GAME", "OOM: %u byte match activity", static_cast<unsigned>(sizeof(GameMatchActivity)));
    failToChoose();  // the goodbye is the match phase's: teardown() sees matchBegun()
    return;
  }
  LOG_INF("GAME", "Nearby: starting the match of %s", manifest.id);
  app.clearTapFlash();
  activityManager.replaceActivity(std::move(match));
}

#endif  // FREEINK_CAP_GAMES
