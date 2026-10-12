// EspNowLinkT (src/games/EspNowLink.h) over a fake radio and a fake air: the bring-up order, a failure at each
// fallible step, PSRAM failure, end(), an unquiesced shutdown, the handle matrix, send refusals, the host label, and
// the contract every ILink passes (LinkContract.h) over two links.
//
// The doubles stand in for ESP-IDF's ESP-NOW and Wi-Fi on one X4 Pro (FakeRadio) and for the air between two
// (FakeAir). Device behaviours they model, each pinned by a test below:
//   - esp_now_init fails while ESP-NOW is already initialised (crossplay's note on its first bug);
//   - esp_wifi_set_channel while the station is associated returns OK and changes nothing, and with auto-reconnect
//     still on a disconnect is undone at once;
//   - esp_now_add_peer of an existing peer is ESP_ERR_ESPNOW_EXIST, which the driver treats as success;
//   - esp_now_deinit drops the peers.
// More permissive than the device: no Wi-Fi-task thread (FrameRingTest covers the ring's two threads), no airtime or
// loss, no 250 B v1 limit, no 20-peer limit, esp_now_send fails only when scripted, no inCallback race. What the host
// cannot show is left to entry 11's device run: the real WiFi and esp_now calls, the restore of the auto-reconnect
// value, the wake window's effect, PSRAM writes from the Wi-Fi task, and heap after teardown.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "EspNowLink.h"
#include "HarnessSupport.h"
#include "LinkContract.h"

namespace {

using EspNow::Mac;
using GameCore::NO_PEER;
using GameCore::PeerHandle;
namespace Protocol = GameCore::Protocol;

enum class Step {
  EnterStation,
  LeaveAccessPoint,
  SetChannel,
  InitEspNow,
  RegisterReceive,
  AddBroadcastPeer,
  EnablePowerSave,
  ReadMac,
  SendTo,
  ShutdownEspNow,
  LeaveStation,
};

// The seven steps that can fail, in bring-up order.
constexpr std::array<Step, 7> FALLIBLE = {Step::EnterStation, Step::LeaveAccessPoint, Step::SetChannel,
                                          Step::InitEspNow,   Step::RegisterReceive,  Step::AddBroadcastPeer,
                                          Step::ReadMac};

constexpr uint8_t AP_CHANNEL = 6;  // the access point the station was joined to before the link took the radio

class FakeAir;

class FakeRadio {
 public:
  FakeRadio(FakeAir& airRef, const Mac macAddress);
  ~FakeRadio();
  FakeRadio(const FakeRadio&) = delete;
  FakeRadio& operator=(const FakeRadio&) = delete;

  // --- scripts ---
  std::optional<Step> failStep;  // this step returns false
  bool failSend = false;         // esp_now_send fails (send queue full)
  bool shutdownStuck = false;    // a receive callback never finishes: shutdownEspNow returns false

  // --- what happened ---
  std::vector<Step> calls;
  struct Sent {
    Mac to;
    std::vector<uint8_t> frame;
  };
  std::vector<Sent> sent;
  size_t count(const Step step) const { return static_cast<size_t>(std::count(calls.begin(), calls.end(), step)); }

  // --- the modelled device ---
  Mac mac;
  bool stationMode = false;
  bool associated = true;
  uint8_t channel = AP_CHANNEL;
  bool autoReconnect = true;
  bool espNowUp = false;
  FrameRing* ring = nullptr;  // the registered receive callback's target
  bool powerSave = false;
  uint16_t wakeWindowMs = 0;
  std::vector<Mac> peers;
  bool broadcastPeerPresent() const { return hasPeer(EspNow::BROADCAST); }

  bool hasPeer(const Mac& m) const {
    return std::any_of(peers.begin(), peers.end(), [&](const Mac& p) { return samePeer(p, m); });
  }
  static bool samePeer(const Mac& a, const Mac& b) { return std::equal(a.bytes, a.bytes + 6, b.bytes); }

  // esp_now_add_peer: ESP_ERR_ESPNOW_EXIST for a peer already there. The driver counts both as success.
  enum class AddPeer { Ok, Exist };
  AddPeer addPeer(const Mac& m) {
    if (hasPeer(m)) return AddPeer::Exist;
    peers.push_back(m);
    return AddPeer::Ok;
  }

  bool enterStation() {
    if (!begin(Step::EnterStation)) return false;
    stationMode = true;
    savedAutoReconnect = autoReconnect;
    autoReconnect = false;
    return true;
  }
  bool leaveAccessPoint() {
    if (!begin(Step::LeaveAccessPoint)) return false;
    // With auto-reconnect still on, an associated station re-associates at once. A station that is started and not
    // connected (the normal case: NearbyProbe makes Wi-Fi off the precondition) is already disconnected, and
    // WiFi.disconnect returns true.
    associated = associated && autoReconnect;
    return true;
  }
  bool setChannel(const uint8_t wanted) {
    if (!begin(Step::SetChannel)) return false;
    if (!associated) channel = wanted;  // while associated: OK, and the AP's channel stays
    return true;
  }
  bool initEspNow() {
    if (!begin(Step::InitEspNow)) return false;
    if (espNowUp) return false;
    espNowUp = true;
    return true;
  }
  bool registerReceive(FrameRing& target) {
    if (!begin(Step::RegisterReceive)) return false;
    ring = &target;
    return true;
  }
  bool addBroadcastPeer() {
    if (!begin(Step::AddBroadcastPeer)) return false;
    (void)addPeer(EspNow::BROADCAST);  // Exist is fine
    return true;
  }
  void enablePowerSave(const uint16_t windowMs) {
    calls.push_back(Step::EnablePowerSave);
    powerSave = true;
    wakeWindowMs = windowMs;
  }
  bool readMac(Mac& out) {
    if (!begin(Step::ReadMac)) return false;
    out = mac;
    return true;
  }
  bool sendTo(const uint8_t* to, const std::span<const uint8_t> frame);  // via the air
  bool shutdownEspNow() {
    calls.push_back(Step::ShutdownEspNow);
    if (!shutdownStuck) ring = nullptr;  // a stuck callback still holds the ring
    espNowUp = false;
    powerSave = false;
    peers.clear();  // esp_now_deinit drops the peers
    return !shutdownStuck;
  }
  // More permissive than the device: there leaveStation is WiFi.mode(WIFI_OFF), which switches the one radio off for
  // everybody. The fake keeps its state per FakeRadio and does not know a second link took the same radio; the
  // device never runs two links (AD-18 gives the radio one owner).
  void leaveStation() {
    calls.push_back(Step::LeaveStation);
    stationMode = false;
    associated = false;
    autoReconnect = savedAutoReconnect;
  }

 private:
  bool begin(const Step step) {
    calls.push_back(step);
    return !(failStep.has_value() && *failStep == step);
  }

  FakeAir& air;
  bool savedAutoReconnect = true;
};

// The medium: a broadcast reaches every other radio that is up on the same channel, a unicast only the radio with
// that MAC, each through FrameRing::push as the receive callback would. Same thread; frames arrive at once.
class FakeAir {
 public:
  void join(FakeRadio* radio) { radios.push_back(radio); }
  void leave(FakeRadio* radio) { radios.erase(std::remove(radios.begin(), radios.end(), radio), radios.end()); }

  void carry(const FakeRadio& from, const Mac& to, const std::span<const uint8_t> frame) {
    const bool broadcast = FakeRadio::samePeer(to, EspNow::BROADCAST);
    for (FakeRadio* radio : radios) {
      if (radio == &from || !radio->espNowUp || radio->ring == nullptr || radio->channel != from.channel) continue;
      if (!broadcast && !FakeRadio::samePeer(radio->mac, to)) continue;
      (void)radio->ring->push(from.mac.bytes, frame.data(), frame.size());
    }
  }

 private:
  std::vector<FakeRadio*> radios;
};

FakeRadio::FakeRadio(FakeAir& airRef, const Mac macAddress) : mac(macAddress), air(airRef) { air.join(this); }
FakeRadio::~FakeRadio() { air.leave(this); }

bool FakeRadio::sendTo(const uint8_t* to, const std::span<const uint8_t> frame) {
  calls.push_back(Step::SendTo);
  if (!espNowUp || failSend) return false;
  Mac target;
  std::copy(to, to + 6, target.bytes);
  if (!hasPeer(target)) (void)addPeer(target);  // the unicast peer is registered on first use
  sent.push_back({target, std::vector<uint8_t>(frame.begin(), frame.end())});
  air.carry(*this, target, frame);
  return true;
}

// The driver the link is built over in these tests: every call goes to the radio.
struct FakeDriver {
  FakeRadio* radio;
  bool enterStation() { return radio->enterStation(); }
  bool leaveAccessPoint() { return radio->leaveAccessPoint(); }
  bool setChannel(const uint8_t channel) { return radio->setChannel(channel); }
  bool initEspNow() { return radio->initEspNow(); }
  bool registerReceive(FrameRing& ring) { return radio->registerReceive(ring); }
  bool addBroadcastPeer(uint8_t) { return radio->addBroadcastPeer(); }
  void enablePowerSave(const uint16_t windowMs) { radio->enablePowerSave(windowMs); }
  bool readMac(Mac& mac) { return radio->readMac(mac); }
  bool sendTo(const uint8_t* mac, const std::span<const uint8_t> frame) { return radio->sendTo(mac, frame); }
  bool shutdownEspNow() { return radio->shutdownEspNow(); }
  void leaveStation() { radio->leaveStation(); }
};

using Link = EspNowLinkT<FakeDriver>;

static_assert(EspNowDriver<FakeDriver>);
static_assert(EspNowDriver<EspIdfDriver>);
struct MissingLeaveStation {
  bool enterStation();
  bool leaveAccessPoint();
  bool setChannel(uint8_t);
  bool initEspNow();
  bool registerReceive(FrameRing&);
  bool addBroadcastPeer(uint8_t);
  void enablePowerSave(uint16_t);
  bool readMac(Mac&);
  bool sendTo(const uint8_t*, std::span<const uint8_t>);
  bool shutdownEspNow();
};
static_assert(!EspNowDriver<MissingLeaveStation>);

Mac macWith(const uint8_t a, const uint8_t b) { return Mac{{0x02, 0x11, 0x22, 0x33, a, b}}; }

std::vector<uint8_t> pattern(const size_t size, const uint8_t seed) {
  std::vector<uint8_t> out(size);
  for (size_t i = 0; i < size; i++) out[i] = static_cast<uint8_t>(seed + i * 3);
  return out;
}

std::string labelText(const std::span<const uint8_t> label) { return std::string(label.begin(), label.end()); }

const std::vector<Step> BRING_UP = {Step::EnterStation,    Step::LeaveAccessPoint, Step::SetChannel,
                                    Step::InitEspNow,      Step::RegisterReceive,  Step::AddBroadcastPeer,
                                    Step::EnablePowerSave, Step::ReadMac};

class EspNowLinkTest : public harness::HarnessTest {
 protected:
  FakeAir air;
  FakeRadio radio{air, macWith(0x3F, 0x2A)};

  std::unique_ptr<Link> create(FakeRadio& r, const std::string_view model = "X4 Pro") {
    return Link::create(FakeDriver{&r}, model);
  }
  std::unique_ptr<Link> create() { return create(radio); }

  // What the receive callback does for a frame heard from `from`.
  static bool hear(FakeRadio& r, const Mac& from, const std::vector<uint8_t>& frame) {
    return r.ring != nullptr && r.ring->push(from.bytes, frame.data(), frame.size());
  }
  static bool take(Link& link, PeerHandle& from, std::vector<uint8_t>& frame) {
    return GameCore::LinkTest::contract::take(link, from, frame);
  }
};

}  // namespace

// ---- the peer table and the label ----

TEST(EspNowPeerTableTest, AttachRules) {
  std::vector<uint8_t> storage(EspNow::PeerTable::storageBytes(16), 0);
  EspNow::PeerTable table;
  EXPECT_FALSE(table.attach(nullptr, storage.size(), 16, 1));
  EXPECT_FALSE(table.attach(storage.data(), storage.size() - 1, 16, 1));
  EXPECT_FALSE(table.attach(storage.data(), storage.size(), 0, 1));
  EXPECT_FALSE(table.attach(storage.data(), storage.size(), 256, 1));
  EXPECT_FALSE(table.attach(storage.data(), storage.size(), 16, 0));
  EXPECT_FALSE(table.attach(storage.data(), storage.size(), 16, 0x1000000));
  EXPECT_TRUE(table.attach(storage.data(), storage.size(), 16, 1));
  EXPECT_TRUE(table.attach(storage.data(), storage.size(), 16, 0xFFFFFF));
}

TEST(EspNowPeerTableTest, HandlesAreStableDistinctAndTaggedWithTheInstance) {
  std::vector<uint8_t> storage(EspNow::PeerTable::storageBytes(2), 0);
  EspNow::PeerTable table;
  ASSERT_TRUE(table.attach(storage.data(), storage.size(), 2, 0xABCDE));
  const Mac a = macWith(1, 1);
  const Mac b = macWith(2, 2);
  const Mac c = macWith(3, 3);
  const PeerHandle ha = table.findOrAdd(a.bytes);
  const PeerHandle hb = table.findOrAdd(b.bytes);
  ASSERT_TRUE(ha.valid());
  ASSERT_TRUE(hb.valid());
  EXPECT_NE(ha, hb);
  EXPECT_EQ(table.findOrAdd(a.bytes), ha);
  EXPECT_EQ(ha.value(), (0xABCDEu << 8) | 1u);
  EXPECT_EQ(hb.value(), (0xABCDEu << 8) | 2u);
  EXPECT_EQ(table.count(), 2u);

  EXPECT_EQ(table.findOrAdd(c.bytes), NO_PEER) << "a full table names no new peer";
  EXPECT_EQ(table.fullDrops(), 1u);
  EXPECT_EQ(table.findOrAdd(c.bytes), NO_PEER);
  EXPECT_EQ(table.fullDrops(), 2u);
  EXPECT_EQ(table.findOrAdd(a.bytes), ha) << "a known peer is still found when the table is full";
  EXPECT_EQ(table.count(), 2u);

  ASSERT_NE(table.macOf(hb), nullptr);
  EXPECT_TRUE(std::equal(b.bytes, b.bytes + 6, table.macOf(hb)));
  EXPECT_EQ(table.macOf(NO_PEER), nullptr);
  EXPECT_EQ(table.macOf(PeerHandle::fromValue((0xABCDEu << 8) | 3u)), nullptr) << "a slot not used yet";
  EXPECT_EQ(table.macOf(PeerHandle::fromValue((0xABCDEu << 8) | 0u)), nullptr);
  EXPECT_EQ(table.macOf(PeerHandle::fromValue((0xABCDFu << 8) | 1u)), nullptr) << "another instance's tag";
  EXPECT_EQ(table.findOrAdd(nullptr), NO_PEER);

  table.detach();
  EXPECT_EQ(table.macOf(ha), nullptr);
  EXPECT_EQ(table.findOrAdd(a.bytes), NO_PEER);
}

TEST(EspNowPeerTableTest, InstanceTagsStayInRangeAndWrapWithinIt) {
  const uint32_t saved = EspNow::nextInstanceTag.load();
  EspNow::nextInstanceTag.store(0xFFFFFD);
  std::vector<uint32_t> tags;
  for (int i = 0; i < 5; i++) tags.push_back(EspNow::takeInstanceTag());
  EspNow::nextInstanceTag.store(saved);
  for (const uint32_t tag : tags) {
    EXPECT_GE(tag, 1u);
    EXPECT_LE(tag, 0xFFFFFFu);
  }
  EXPECT_EQ(tags[0], 0xFFFFFEu);
  EXPECT_EQ(tags[1], 0xFFFFFFu);
  EXPECT_EQ(tags[2], 1u) << "wraps to 1, never 0";
  EXPECT_EQ(tags[3], 2u);
}

TEST(EspNowLabelTest, ModelAndTheLastTwoMacBytesInUpperCaseHex) {
  EXPECT_EQ(labelText(EspNow::makeLabel("X4 Pro", macWith(0x3F, 0x2A)).view()), "X4 Pro 3F2A");
  EXPECT_EQ(labelText(EspNow::makeLabel("Sticky", macWith(0x00, 0xFF)).view()), "Sticky 00FF");
  EXPECT_EQ(labelText(EspNow::makeLabel("Reader", macWith(0xab, 0xcd)).view()), "Reader ABCD");
}

TEST(EspNowLabelTest, TheModelIsCutSoTheWholeIsAtMostSixteenBytes) {
  const auto label = EspNow::makeLabel("ABCDEFGHIJKLMNOP", macWith(0x12, 0x34));
  EXPECT_EQ(labelText(label.view()), "ABCDEFGHIJK 1234");
  EXPECT_EQ(label.length, Protocol::MAX_HOST_LABEL_BYTES);
  // Exactly 11 model bytes fit whole.
  EXPECT_EQ(labelText(EspNow::makeLabel("ABCDEFGHIJK", macWith(0x12, 0x34)).view()), "ABCDEFGHIJK 1234");
  // A cut never ends on half of a two-byte character: 10 characters of 2 bytes, a cut at 11 backs off to 10.
  const std::string accented = "\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89";
  const auto cut = EspNow::makeLabel(accented, macWith(0x12, 0x34));
  EXPECT_EQ(labelText(cut.view()), accented.substr(0, 10) + " 1234");
  EXPECT_EQ(EspNow::makeLabel("", macWith(0x12, 0x34)).length, 4u);
  EXPECT_EQ(labelText(EspNow::makeLabel("", macWith(0x12, 0x34)).view()), "1234");
}

// ---- bring-up ----

TEST_F(EspNowLinkTest, BringsUpInTheOrderAD18Gives) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  EXPECT_EQ(radio.calls, BRING_UP);

  // The radio ends up as AD-18 wants it: station only, off the access point, on channel 1, ESP-NOW up with the
  // broadcast peer, power save with the wake window, auto-reconnect off.
  EXPECT_TRUE(radio.stationMode);
  EXPECT_FALSE(radio.associated);
  EXPECT_EQ(radio.channel, EspNow::CHANNEL) << "a channel set while associated is silently reverted";
  EXPECT_TRUE(radio.espNowUp);
  EXPECT_TRUE(radio.broadcastPeerPresent());
  EXPECT_TRUE(radio.powerSave);
  EXPECT_EQ(radio.wakeWindowMs, 50u);
  EXPECT_FALSE(radio.autoReconnect);
  EXPECT_NE(radio.ring, nullptr);
  EXPECT_EQ(labelText(link->hostLabel()), "X4 Pro 3F2A");
  EXPECT_LE(link->hostLabel().size(), Protocol::MAX_HOST_LABEL_BYTES);
  EXPECT_FALSE(fakelog::any("ERR LINK"));
}

TEST_F(EspNowLinkTest, TheReceiveRingAndPeerTableAreOnePsramBlockAllocatedBeforeTheRadioIsTouched) {
  static_assert(EspNow::RING_SLOTS_BYTES == 11840);
  static_assert(EspNow::TABLE_BYTES == 96);
  static_assert(EspNow::BLOCK_BYTES == EspNow::RING_CONTROL_BYTES + 11840 + 96);
  auto link = create();
  ASSERT_NE(link, nullptr);
  EXPECT_EQ(fakepsram::totalAllocations, 1u);
  EXPECT_EQ(fakepsram::liveBlocks, 1u);
  EXPECT_EQ(fakepsram::lastRequestedBytes, EspNow::BLOCK_BYTES);

  // The registered ring is inside that block: a full ring's worth of the largest frames stays inside its guards.
  for (int i = 0; i < 8; i++) ASSERT_TRUE(hear(radio, macWith(1, 1), pattern(Protocol::MAX_FRAME_BYTES, 7)));
  EXPECT_FALSE(hear(radio, macWith(1, 1), pattern(10, 1)));
  EXPECT_EQ(link->ringOverflows(), 1u);
  link.reset();
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(EspNowLinkTest, TheModelShownIsWhatTheFactoryIsGiven) {
  auto link = create(radio, "Sticky");
  ASSERT_NE(link, nullptr);
  EXPECT_EQ(labelText(link->hostLabel()), "Sticky 3F2A");
}

TEST_F(EspNowLinkTest, AFailureAtAnyFallibleStepTearsDownAndTheNextBringUpSucceeds) {
  ASSERT_EQ(FALLIBLE.size(), 7u);
  for (const Step failing : FALLIBLE) {
    SCOPED_TRACE("failing step " + std::to_string(static_cast<int>(failing)));
    radio.calls.clear();
    radio.failStep = failing;
    fakelog::lines.clear();

    EXPECT_EQ(create(), nullptr);
    const bool initSucceeded = failing != Step::EnterStation && failing != Step::LeaveAccessPoint &&
                               failing != Step::SetChannel && failing != Step::InitEspNow;
    EXPECT_EQ(radio.count(Step::ShutdownEspNow), initSucceeded ? 1u : 0u) << "ESP-NOW is deinitialized iff init worked";
    EXPECT_EQ(radio.count(Step::LeaveStation), 1u) << "the station is handed back exactly once";
    EXPECT_FALSE(radio.espNowUp);
    EXPECT_FALSE(radio.stationMode);
    EXPECT_TRUE(radio.autoReconnect) << "the previous auto-reconnect is restored";
    EXPECT_EQ(radio.ring, nullptr);
    EXPECT_EQ(fakepsram::liveBlocks, 0u) << "the PSRAM block is freed";
    EXPECT_EQ(fakepsram::overruns, 0u);
    EXPECT_TRUE(fakelog::any("ERR LINK")) << "a failed bring-up is logged";

    // The next bring-up on the same radio works: no ESP-NOW left up to fail esp_now_init.
    radio.failStep.reset();
    radio.calls.clear();
    radio.associated = true;  // as if joined to the access point again
    radio.channel = AP_CHANNEL;
    auto link = create();
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(radio.calls, BRING_UP);
    EXPECT_EQ(radio.channel, EspNow::CHANNEL);
    link.reset();
    EXPECT_EQ(fakepsram::liveBlocks, 0u);
  }
}

TEST_F(EspNowLinkTest, AFailedBringUpNamesTheStepAndLeavesNoCallbackBehind) {
  radio.failStep = Step::AddBroadcastPeer;
  EXPECT_EQ(create(), nullptr);
  EXPECT_TRUE(fakelog::any("addBroadcastPeer"));
  EXPECT_EQ(radio.ring, nullptr) << "the receive callback must be unregistered";
}

TEST_F(EspNowLinkTest, NoPsramMeansNullBeforeAnyDriverCall) {
  fakepsram::failNext = true;
  EXPECT_EQ(create(), nullptr);
  EXPECT_TRUE(radio.calls.empty());
  EXPECT_TRUE(fakelog::any("ERR LINK"));
  EXPECT_TRUE(fakelog::any(std::to_string(EspNow::BLOCK_BYTES))) << "the log names the size asked for";
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_TRUE(radio.associated) << "the radio was never touched";

  auto link = create();
  EXPECT_NE(link, nullptr) << "the next bring-up succeeds";
}

TEST_F(EspNowLinkTest, EndIsIdempotentAndLeavesTheRadioOff) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  radio.calls.clear();
  link->end();
  EXPECT_EQ(radio.calls, (std::vector<Step>{Step::ShutdownEspNow, Step::LeaveStation}));
  EXPECT_FALSE(radio.espNowUp);
  EXPECT_FALSE(radio.stationMode);
  EXPECT_TRUE(radio.autoReconnect);
  EXPECT_EQ(radio.ring, nullptr);

  radio.calls.clear();
  link->end();
  link->end();
  link.reset();  // the destructor after end() too
  EXPECT_TRUE(radio.calls.empty()) << "a second end() must not touch the driver";
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
}

TEST_F(EspNowLinkTest, DestroyingARunningLinkEndsItOnce) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  radio.calls.clear();
  link.reset();
  EXPECT_EQ(radio.calls, (std::vector<Step>{Step::ShutdownEspNow, Step::LeaveStation}));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
}

TEST_F(EspNowLinkTest, AnEndedLinkRefusesEverythingAndDiscardsWhatWaited) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  ASSERT_TRUE(hear(radio, macWith(9, 9), pattern(30, 1)));
  PeerHandle from;
  std::vector<uint8_t> frame;
  // Name the peer first, so a handle exists to be dead afterwards.
  ASSERT_TRUE(take(*link, from, frame));
  ASSERT_TRUE(hear(radio, macWith(9, 9), pattern(30, 2)));
  link->end();
  EXPECT_FALSE(link->send(from, pattern(10, 1)));
  EXPECT_FALSE(link->broadcast(pattern(10, 1)));
  EXPECT_FALSE(take(*link, from, frame));
  EXPECT_EQ(radio.count(Step::SendTo), 0u);
}

// More permissive than the device: there the failed second create's LeaveStation is WiFi.mode(WIFI_OFF), which
// would take the first link's radio down too; the fake leaves the first link's ESP-NOW up. The device never runs two
// links (AD-18 gives the radio one owner), so this pins only the call sequence of the second bring-up.
TEST_F(EspNowLinkTest, ASecondBringUpOnARadioThatIsStillUpFailsAtInit) {
  auto first = create();
  ASSERT_NE(first, nullptr);
  radio.calls.clear();
  EXPECT_EQ(create(), nullptr);
  EXPECT_EQ(radio.calls, (std::vector<Step>{Step::EnterStation, Step::LeaveAccessPoint, Step::SetChannel,
                                            Step::InitEspNow, Step::LeaveStation}))
      << "esp_now_init fails while initialised, and the second link must not deinitialize the first's ESP-NOW";
  EXPECT_TRUE(radio.espNowUp);
  EXPECT_NE(radio.ring, nullptr);
}

TEST_F(EspNowLinkTest, ABringUpFromWifiOffNeverAssociatedWorksAndEndsWithTheRadioOff) {
  radio.associated = false;
  radio.stationMode = false;
  auto link = create();
  ASSERT_NE(link, nullptr);
  EXPECT_EQ(radio.calls, BRING_UP);
  EXPECT_EQ(radio.channel, EspNow::CHANNEL);
  EXPECT_TRUE(radio.espNowUp);
  EXPECT_TRUE(radio.stationMode);

  link->end();
  EXPECT_FALSE(radio.stationMode);
  EXPECT_FALSE(radio.espNowUp);
  EXPECT_FALSE(radio.associated);
  EXPECT_TRUE(radio.autoReconnect);
  EXPECT_EQ(radio.ring, nullptr);
}

TEST_F(EspNowLinkTest, AnExistingBroadcastPeerIsNotAFailure) {
  radio.peers.push_back(EspNow::BROADCAST);
  auto link = create();
  EXPECT_NE(link, nullptr);
}

TEST_F(EspNowLinkTest, TheFakeRadioModelsTheDeviceBehavioursItStandsFor) {
  // set_channel while associated: OK and unchanged.
  EXPECT_TRUE(radio.setChannel(1));
  EXPECT_EQ(radio.channel, AP_CHANNEL);
  // With auto-reconnect still on, a disconnect is undone.
  EXPECT_TRUE(radio.leaveAccessPoint());
  EXPECT_TRUE(radio.associated);
  EXPECT_TRUE(radio.enterStation());
  EXPECT_TRUE(radio.leaveAccessPoint());
  EXPECT_FALSE(radio.associated);
  EXPECT_TRUE(radio.setChannel(1));
  EXPECT_EQ(radio.channel, 1);
  // esp_now_init twice fails; deinit drops the peers.
  EXPECT_TRUE(radio.initEspNow());
  EXPECT_FALSE(radio.initEspNow());
  EXPECT_EQ(radio.addPeer(EspNow::BROADCAST), FakeRadio::AddPeer::Ok);
  EXPECT_EQ(radio.addPeer(EspNow::BROADCAST), FakeRadio::AddPeer::Exist);
  EXPECT_TRUE(radio.shutdownEspNow());
  EXPECT_FALSE(radio.broadcastPeerPresent());
}

TEST_F(EspNowLinkTest, AnUnquiescedShutdownAbandonsTheStorageInsteadOfFreeingIt) {
  expectCleanPsram = false;  // the leak is the point
  radio.shutdownStuck = true;
  auto link = create();
  ASSERT_NE(link, nullptr);
  FrameRing* const ring = radio.ring;
  ASSERT_NE(ring, nullptr);
  link.reset();

  EXPECT_EQ(radio.count(Step::ShutdownEspNow), 1u);
  EXPECT_EQ(radio.count(Step::LeaveStation), 1u) << "Wi-Fi is handed back even then";
  EXPECT_EQ(fakepsram::liveBlocks, 1u) << "the block a callback may still write must not be freed";
  EXPECT_TRUE(fakelog::any("abandoned"));
  // The callback that was still running pushes into memory that is still there, ring object included.
  EXPECT_TRUE(ring->push(macWith(1, 1).bytes, pattern(40, 1).data(), 40));
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(EspNowLinkTest, AFailedBringUpWithAStuckShutdownAbandonsToo) {
  expectCleanPsram = false;
  radio.shutdownStuck = true;
  radio.failStep = Step::ReadMac;
  EXPECT_EQ(create(), nullptr);
  EXPECT_EQ(fakepsram::liveBlocks, 1u);
  EXPECT_EQ(radio.count(Step::LeaveStation), 1u);
}

// ---- handles, receive, send ----

TEST_F(EspNowLinkTest, OnePeerHeardTwiceGivesEqualHandlesAndNeverNoPeer) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  const Mac peer = macWith(7, 7);
  PeerHandle first, second, other;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(hear(radio, peer, pattern(20, 1)));
  ASSERT_TRUE(take(*link, first, frame));
  EXPECT_EQ(frame, pattern(20, 1));
  ASSERT_TRUE(hear(radio, peer, pattern(21, 2)));
  ASSERT_TRUE(take(*link, second, frame));
  EXPECT_EQ(frame, pattern(21, 2));
  ASSERT_TRUE(hear(radio, macWith(8, 8), pattern(22, 3)));
  ASSERT_TRUE(take(*link, other, frame));

  EXPECT_TRUE(first.valid());
  EXPECT_NE(first, NO_PEER);
  EXPECT_EQ(first, second);
  EXPECT_NE(first, other);
  EXPECT_NE(other, NO_PEER);
  EXPECT_EQ(first.value() & 0xFF, 1u);
  EXPECT_EQ(other.value() & 0xFF, 2u);
  EXPECT_NE(first.value() >> 8, 0u) << "the instance tag sits above the slot";
}

TEST_F(EspNowLinkTest, ASendGoesToTheMacTheHandleNames) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  PeerHandle a, b;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(hear(radio, macWith(1, 1), pattern(5, 1)));
  ASSERT_TRUE(take(*link, a, frame));
  ASSERT_TRUE(hear(radio, macWith(2, 2), pattern(5, 1)));
  ASSERT_TRUE(take(*link, b, frame));

  ASSERT_TRUE(link->send(b, pattern(33, 4)));
  ASSERT_TRUE(link->send(a, pattern(34, 5)));
  ASSERT_TRUE(link->broadcast(pattern(35, 6)));
  ASSERT_EQ(radio.sent.size(), 3u);
  EXPECT_TRUE(FakeRadio::samePeer(radio.sent[0].to, macWith(2, 2)));
  EXPECT_EQ(radio.sent[0].frame, pattern(33, 4));
  EXPECT_TRUE(FakeRadio::samePeer(radio.sent[1].to, macWith(1, 1)));
  EXPECT_TRUE(FakeRadio::samePeer(radio.sent[2].to, EspNow::BROADCAST));
  EXPECT_EQ(radio.sent[2].frame, pattern(35, 6));
}

TEST_F(EspNowLinkTest, AHandleFromAnotherLinkInstanceNamesNoPeerEvenAtTheSameSlot) {
  FakeRadio otherRadio{air, macWith(0x5A, 0x5B)};
  auto a = create(radio);
  auto b = create(otherRadio);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  // Both hear one peer, so both have a slot 1; only the instance tag tells their handles apart.
  PeerHandle aNamesPeer, bNamesPeer;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(hear(radio, macWith(1, 1), pattern(5, 1)));
  ASSERT_TRUE(take(*a, aNamesPeer, frame));
  ASSERT_TRUE(hear(otherRadio, macWith(1, 1), pattern(5, 1)));
  ASSERT_TRUE(take(*b, bNamesPeer, frame));
  EXPECT_NE(aNamesPeer, bNamesPeer);
  EXPECT_NE(aNamesPeer.value() >> 8, bNamesPeer.value() >> 8);

  EXPECT_FALSE(b->send(aNamesPeer, pattern(10, 1)));
  EXPECT_FALSE(a->send(bNamesPeer, pattern(10, 1)));
  EXPECT_EQ(otherRadio.count(Step::SendTo), 0u);
  EXPECT_EQ(radio.count(Step::SendTo), 0u);
  EXPECT_TRUE(a->send(aNamesPeer, pattern(10, 1)));
}

TEST_F(EspNowLinkTest, InvalidAndMadeUpHandlesAreRefused) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  PeerHandle named;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(hear(radio, macWith(1, 1), pattern(5, 1)));
  ASSERT_TRUE(take(*link, named, frame));

  EXPECT_FALSE(link->send(NO_PEER, pattern(10, 1)));
  EXPECT_FALSE(link->send(PeerHandle::fromValue(1), pattern(10, 1)));
  EXPECT_FALSE(link->send(PeerHandle::fromValue(0xFFFFFFFF), pattern(10, 1)));
  EXPECT_FALSE(link->send(PeerHandle::fromValue(named.value() + 1), pattern(10, 1))) << "a slot not yet used";
  EXPECT_EQ(radio.count(Step::SendTo), 0u);
}

TEST_F(EspNowLinkTest, ASeventeenthPeerIsNotNamedAndItsFramesAreDroppedAndCounted) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  std::vector<PeerHandle> handles;
  for (uint8_t i = 1; i <= 16; i++) {
    PeerHandle h;
    std::vector<uint8_t> frame;
    ASSERT_TRUE(hear(radio, macWith(i, i), pattern(8, i)));
    ASSERT_TRUE(take(*link, h, frame));
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(std::count(handles.begin(), handles.end(), h), 0) << "peer " << int{i};
    handles.push_back(h);
  }
  EXPECT_EQ(link->unnamedPeerDrops(), 0u);

  // The 17th peer, then a frame from a known peer behind it.
  ASSERT_TRUE(hear(radio, macWith(17, 17), pattern(8, 17)));
  ASSERT_TRUE(hear(radio, macWith(3, 3), pattern(9, 33)));
  PeerHandle h = NO_PEER;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(take(*link, h, frame)) << "receive goes on past a frame it cannot name";
  EXPECT_EQ(h, handles[2]);
  EXPECT_EQ(frame, pattern(9, 33));
  EXPECT_EQ(link->unnamedPeerDrops(), 1u);

  ASSERT_TRUE(hear(radio, macWith(17, 17), pattern(8, 18)));
  EXPECT_FALSE(take(*link, h, frame));
  EXPECT_EQ(link->unnamedPeerDrops(), 2u);
  EXPECT_NE(h, NO_PEER) << "a false receive leaves `from` as it was";
}

TEST_F(EspNowLinkTest, ReceiveDropsAFrameLongerThanTheBufferAndGoesOn) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  ASSERT_TRUE(hear(radio, macWith(1, 1), pattern(100, 4)));
  ASSERT_TRUE(hear(radio, macWith(2, 2), pattern(20, 6)));
  std::vector<uint8_t> small(50, 0xAA);
  PeerHandle from;
  size_t length = 0;
  ASSERT_TRUE(link->receive(from, small, length));
  EXPECT_EQ(length, 20u);
  EXPECT_TRUE(std::equal(small.begin(), small.begin() + 20, pattern(20, 6).begin()));
  EXPECT_FALSE(link->receive(from, small, length));
}

TEST_F(EspNowLinkTest, AFullRingCostsAFrameAndIsCounted) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  for (int i = 0; i < 8; i++) ASSERT_TRUE(hear(radio, macWith(1, 1), pattern(50, static_cast<uint8_t>(i))));
  EXPECT_FALSE(hear(radio, macWith(1, 1), pattern(50, 99)));
  EXPECT_FALSE(hear(radio, macWith(1, 1), pattern(50, 98)));
  EXPECT_EQ(link->ringOverflows(), 2u);
  PeerHandle from;
  std::vector<uint8_t> frame;
  for (int i = 0; i < 8; i++) {
    ASSERT_TRUE(take(*link, from, frame));
    EXPECT_EQ(frame, pattern(50, static_cast<uint8_t>(i)));
  }
  EXPECT_FALSE(take(*link, from, frame));
}

TEST_F(EspNowLinkTest, SendRefusalsNeverReachTheDriverExceptAFullSendQueue) {
  auto link = create();
  ASSERT_NE(link, nullptr);
  PeerHandle peer;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(hear(radio, macWith(1, 1), pattern(5, 1)));
  ASSERT_TRUE(take(*link, peer, frame));

  const std::vector<uint8_t> tooBig = pattern(Protocol::MAX_FRAME_BYTES + 1, 2);
  EXPECT_FALSE(link->send(peer, std::span<const uint8_t>()));
  EXPECT_FALSE(link->send(peer, tooBig));
  EXPECT_FALSE(link->broadcast(std::span<const uint8_t>()));
  EXPECT_FALSE(link->broadcast(tooBig));
  EXPECT_EQ(radio.count(Step::SendTo), 0u) << "over-limit and empty frames never reach the driver";

  EXPECT_TRUE(link->send(peer, pattern(Protocol::MAX_FRAME_BYTES, 3))) << "the limit itself is accepted";
  EXPECT_TRUE(link->send(peer, pattern(1, 3)));

  radio.failSend = true;  // the send queue is full
  EXPECT_FALSE(link->send(peer, pattern(10, 1)));
  EXPECT_FALSE(link->broadcast(pattern(10, 1)));
  radio.failSend = false;
  EXPECT_TRUE(link->broadcast(pattern(10, 1)));
}

TEST_F(EspNowLinkTest, TwoLinksOnOneAirPassTheLinkContract) {
  FakeRadio otherRadio{air, macWith(0x5A, 0x5B)};
  auto a = create(radio);
  auto b = create(otherRadio, "Sticky");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_NE(labelText(a->hostLabel()), labelText(b->hostLabel()));
  GameCore::LinkTest::expectLinkContract(*a, *b, [] {});
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(EspNowLinkTest, ALinkOnAnotherChannelHearsNothing) {
  // The air only carries between radios on the same channel, so a bring-up that left the radio on the access point's
  // channel (the set_channel-while-associated bug) is a deaf link, not a silent pass.
  FakeRadio otherRadio{air, macWith(0x5A, 0x5B)};
  auto a = create(radio);
  ASSERT_NE(a, nullptr);
  auto b = create(otherRadio);
  ASSERT_NE(b, nullptr);
  otherRadio.channel = AP_CHANNEL;
  ASSERT_TRUE(a->broadcast(pattern(10, 1)));
  PeerHandle from;
  std::vector<uint8_t> frame;
  EXPECT_FALSE(take(*b, from, frame));
}
