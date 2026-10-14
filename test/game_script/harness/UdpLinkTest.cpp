#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../../game_core/LinkContract.h"
#include "NearbySessionSupport.h"
#include "UdpLink.h"

// UdpLink, the simulator's ILink over UDP on loopback (entry 16 of epic-play-nearby), over real sockets. The raw socket
// below is a test tool that stands for another process on the port range, not a double. Every test picks its own base,
// all eight ports probed free, and sets the base variable itself, so tests (and test processes under ctest -j) do not
// hear each other.
//
// More permissive than a simulator window pair: the NearbySession test runs on the fake RTOS clock
// (NearbySessionSupport.h says why a bound in fake milliseconds is shorter in real time than on a device), so no host
// timing bound is asserted.

namespace {

using namespace nearby;
using GameCore::LinkTest::expectLinkContract;
using GameCore::LinkTest::expectThreeLinkContract;
using GameCore::Protocol::MAX_FRAME_BYTES;

const std::function<void()> PUMP = [] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); };

sockaddr_in loopback(const uint16_t port) {
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  return address;
}

// A UDP socket on loopback that is not a UdpLink: the stranger, or a process that owns a port of the range.
class RawSocket {
 public:
  // Bound to `port`, or to any free port when 0. Not open (fd < 0) when the bind failed.
  explicit RawSocket(const uint16_t port) : fd(::socket(AF_INET, SOCK_DGRAM, 0)) {
    if (fd < 0) return;
    const sockaddr_in address = loopback(port);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      ::close(fd);
      fd = -1;
      return;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
  ~RawSocket() {
    if (fd >= 0) ::close(fd);
  }
  RawSocket(const RawSocket&) = delete;
  RawSocket& operator=(const RawSocket&) = delete;

  bool open() const { return fd >= 0; }
  bool sendTo(const uint16_t port, const size_t bytes) const {
    const std::vector<uint8_t> data(bytes, 0x5A);
    const sockaddr_in destination = loopback(port);
    return ::sendto(fd, data.data(), bytes, 0, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) ==
           static_cast<ssize_t>(bytes);
  }
  // True when a datagram is waiting (and takes it).
  bool hasDatagram() const {
    uint8_t scratch[2048];
    return ::recv(fd, scratch, sizeof(scratch), 0) >= 0;
  }

 private:
  int fd;
};

// True when all eight ports from `base` can be bound now.
bool rangeFree(const uint16_t base) {
  for (size_t slot = 0; slot < UdpLink::SLOTS; slot++) {
    RawSocket probe(static_cast<uint16_t>(base + slot));
    if (!probe.open()) return false;
  }
  return true;
}

// A per-process free base: derived from the process id, all eight ports probed free, retried from another candidate.
uint16_t freeBase() {
  const uint32_t pid = static_cast<uint32_t>(::getpid());
  for (uint32_t attempt = 0; attempt < 200; attempt++) {
    // 11000..20992: below sim.sh's bases (21000..28992) and Linux's ephemeral ports (32768 and up).
    const uint16_t base = static_cast<uint16_t>(11000 + ((pid * 37u + attempt * 101u) % 1250u) * 8u);
    if (rangeFree(base)) return base;
  }
  ADD_FAILURE() << "no free range of eight ports";
  return 0;
}

// Sets the base variable for a test's life and restores what was there.
class BaseVariable {
 public:
  explicit BaseVariable(const char* value) {
    const char* const old = std::getenv(UdpLink::BASE_PORT_ENV);
    hadOld = old != nullptr;
    if (hadOld) oldValue = old;
    set(value);
  }
  ~BaseVariable() { set(hadOld ? oldValue.c_str() : nullptr); }
  BaseVariable(const BaseVariable&) = delete;
  BaseVariable& operator=(const BaseVariable&) = delete;

  static void set(const char* value) {
    if (value == nullptr) {
      ::unsetenv(UdpLink::BASE_PORT_ENV);
    } else {
      ::setenv(UdpLink::BASE_PORT_ENV, value, 1);
    }
  }
  static void set(const uint16_t port) { set(std::to_string(port).c_str()); }

 private:
  bool hadOld = false;
  std::string oldValue;
};

// A base that is free, in the variable, for the test's life.
struct Range {
  Range() : base(freeBase()), variable(std::to_string(base).c_str()) {}
  uint16_t base;
  BaseVariable variable;
};

std::unique_ptr<UdpLink> mustCreate(const uint16_t base) {
  std::unique_ptr<UdpLink> link = UdpLink::create(base);
  EXPECT_NE(link, nullptr);
  return link;
}

std::string labelText(const UdpLink& link) {
  const auto label = link.hostLabel();
  return std::string(label.begin(), label.end());
}

// Takes one frame from `link` into a full-size buffer; the sender is `from`.
bool takeOne(GameCore::ILink& link, GameCore::PeerHandle& from, std::vector<uint8_t>& frame) {
  return GameCore::LinkTest::contract::take(link, from, frame);
}

// ---------------------------------------------------------------- the contract

TEST(UdpLinkTest, TwoLinksTakeTheFirstTwoPortsAndPassTheLinkContract) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto a = mustCreate(range.base);
  auto b = mustCreate(range.base);
  ASSERT_TRUE(a && b);
  EXPECT_EQ(a->port(), range.base);
  EXPECT_EQ(b->port(), range.base + 1);
  expectLinkContract(*a, *b, PUMP);
}

TEST(UdpLinkTest, ThreeLinksPassTheThreeLinkContract) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto a = mustCreate(range.base);
  auto b = mustCreate(range.base);
  auto c = mustCreate(range.base);
  ASSERT_TRUE(a && b && c);
  EXPECT_EQ(c->port(), range.base + 2);
  expectThreeLinkContract(*a, *b, *c, PUMP);
}

TEST(UdpLinkTest, EightLinksHaveDistinctPortsTagsAndHandlesAndANinthIsRefused) {
  Range range;
  ASSERT_NE(range.base, 0);
  std::vector<std::unique_ptr<UdpLink>> links;
  std::set<uint16_t> ports;
  for (size_t i = 0; i < UdpLink::SLOTS; i++) {
    links.push_back(mustCreate(range.base));
    ASSERT_TRUE(links.back());
    ports.insert(links.back()->port());
  }
  EXPECT_EQ(ports.size(), UdpLink::SLOTS);
  EXPECT_EQ(*ports.begin(), range.base);
  EXPECT_EQ(*ports.rbegin(), range.base + UdpLink::SLOTS - 1);

  // Each link broadcasts once, so each of the others names it.
  for (auto& link : links) ASSERT_TRUE(link->broadcast(GameCore::LinkTest::contract::pattern(16, 3)));
  PUMP();
  std::set<uint32_t> everyValue;
  for (size_t listener = 0; listener < links.size(); listener++) {
    std::set<uint32_t> mine;
    GameCore::PeerHandle from;
    std::vector<uint8_t> frame;
    while (takeOne(*links[listener], from, frame)) {
      EXPECT_TRUE(from.valid());
      mine.insert(from.value());
      everyValue.insert(from.value());
    }
    EXPECT_EQ(mine.size(), UdpLink::SLOTS - 1) << "listener " << listener << " must name seven distinct senders";
  }
  EXPECT_EQ(everyValue.size(), UdpLink::SLOTS * (UdpLink::SLOTS - 1)) << "handle values repeat across links";

  // The ninth has no port; one end() frees one for the next.
  fakelog::clearLines();
  EXPECT_EQ(UdpLink::create(range.base), nullptr);
  EXPECT_TRUE(fakelog::anyLine("ERR LINK: no free udp port"));
  links[3]->end();
  auto again = mustCreate(range.base);
  ASSERT_TRUE(again);
  EXPECT_EQ(again->port(), range.base + 3);
}

// ---------------------------------------------------------------- what a link drops

TEST(UdpLinkTest, AnOversizeDatagramIsDroppedAndTheNextOneIsDelivered) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto link = mustCreate(range.base);
  ASSERT_TRUE(link);
  RawSocket raw(static_cast<uint16_t>(range.base + 2));  // inside the range, so only its size makes it a stranger
  ASSERT_TRUE(raw.open());

  ASSERT_TRUE(raw.sendTo(link->port(), 2000));
  ASSERT_TRUE(raw.sendTo(link->port(), 10));
  PUMP();
  GameCore::PeerHandle from;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(takeOne(*link, from, frame));
  EXPECT_EQ(frame.size(), 10u) << "the 2,000 B datagram must not arrive, whole or cut";
  EXPECT_TRUE(from.valid());
  EXPECT_FALSE(takeOne(*link, from, frame));

  // Exactly the largest frame is delivered; one byte more is dropped.
  ASSERT_TRUE(raw.sendTo(link->port(), MAX_FRAME_BYTES + 1));
  ASSERT_TRUE(raw.sendTo(link->port(), MAX_FRAME_BYTES));
  PUMP();
  ASSERT_TRUE(takeOne(*link, from, frame));
  EXPECT_EQ(frame.size(), MAX_FRAME_BYTES);
  EXPECT_FALSE(takeOne(*link, from, frame));
}

TEST(UdpLinkTest, ADatagramOverTheLargestFrameIsDroppedEvenForABiggerBuffer) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto link = mustCreate(range.base);
  ASSERT_TRUE(link);
  RawSocket raw(static_cast<uint16_t>(range.base + 2));
  ASSERT_TRUE(raw.open());
  ASSERT_TRUE(raw.sendTo(link->port(), MAX_FRAME_BYTES + 1));
  ASSERT_TRUE(raw.sendTo(link->port(), 2000));
  ASSERT_TRUE(raw.sendTo(link->port(), 100));
  PUMP();
  std::vector<uint8_t> big(4000, 0);
  GameCore::PeerHandle from;
  size_t length = 0;
  ASSERT_TRUE(link->receive(from, big, length));
  EXPECT_EQ(length, 100u) << "neither oversize datagram may arrive, though the buffer could hold them";
  EXPECT_FALSE(link->receive(from, big, length));
}

TEST(UdpLinkTest, AFrameLongerThanTheCallersBufferIsDroppedNotCut) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto a = mustCreate(range.base);
  auto b = mustCreate(range.base);
  ASSERT_TRUE(a && b);
  ASSERT_TRUE(b->broadcast(GameCore::LinkTest::contract::pattern(100, 1)));
  ASSERT_TRUE(b->broadcast(GameCore::LinkTest::contract::pattern(20, 2)));
  PUMP();
  std::vector<uint8_t> small(50, 0xAA);
  GameCore::PeerHandle from;
  size_t length = 0;
  ASSERT_TRUE(a->receive(from, small, length));
  EXPECT_EQ(length, 20u);
  EXPECT_TRUE(std::equal(small.begin(), small.begin() + 20, GameCore::LinkTest::contract::pattern(20, 2).begin()));
  EXPECT_FALSE(a->receive(from, small, length));
  // A buffer of no bytes drops every datagram and says false.
  ASSERT_TRUE(b->broadcast(GameCore::LinkTest::contract::pattern(5, 3)));
  PUMP();
  EXPECT_FALSE(a->receive(from, std::span<uint8_t>(), length));
}

TEST(UdpLinkTest, AStrangerAndAnEmptyDatagramAreDropped) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto link = mustCreate(range.base);
  ASSERT_TRUE(link);
  RawSocket stranger(static_cast<uint16_t>(range.base + UdpLink::SLOTS));  // the port just past the range
  ASSERT_TRUE(stranger.open());
  RawSocket neighbour(static_cast<uint16_t>(range.base + 1));
  ASSERT_TRUE(neighbour.open());

  ASSERT_TRUE(stranger.sendTo(link->port(), 30));
  ASSERT_TRUE(neighbour.sendTo(link->port(), 0));
  ASSERT_TRUE(neighbour.sendTo(link->port(), 12));
  PUMP();
  GameCore::PeerHandle from;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(takeOne(*link, from, frame));
  EXPECT_EQ(frame.size(), 12u) << "the stranger's and the empty datagram must not arrive";
  EXPECT_FALSE(takeOne(*link, from, frame));
}

TEST(UdpLinkTest, AForeignOrDeadOrUnheardHandleSendsNothing) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto a = mustCreate(range.base);
  auto b = mustCreate(range.base);
  ASSERT_TRUE(a && b);
  RawSocket unheard(static_cast<uint16_t>(range.base + 5));  // a port of the range that has sent nothing
  ASSERT_TRUE(unheard.open());

  ASSERT_TRUE(b->broadcast(GameCore::LinkTest::contract::pattern(8, 1)));
  PUMP();
  GameCore::PeerHandle bNamed;
  std::vector<uint8_t> frame;
  ASSERT_TRUE(takeOne(*a, bNamed, frame));
  while (unheard.hasDatagram()) {
  }  // b's broadcast reached the raw socket too; what follows must add nothing
  const uint32_t tagBits = bNamed.value() & 0xFFFF0000u;
  const std::vector<uint8_t> hello = GameCore::LinkTest::contract::pattern(8, 2);

  EXPECT_FALSE(a->send(GameCore::NO_PEER, hello));
  EXPECT_FALSE(a->send(GameCore::PeerHandle::fromValue(tagBits | (range.base + 5)), hello)) << "an unheard port";
  EXPECT_FALSE(a->send(GameCore::PeerHandle::fromValue(tagBits | a->port()), hello)) << "its own port";
  EXPECT_FALSE(a->send(GameCore::PeerHandle::fromValue(tagBits | (range.base + UdpLink::SLOTS)), hello))
      << "a port outside the range";
  EXPECT_FALSE(a->send(GameCore::PeerHandle::fromValue(bNamed.value() + 0x10000u), hello)) << "another tag";
  EXPECT_FALSE(b->send(bNamed, hello)) << "a's handle for b, on b";
  PUMP();
  EXPECT_FALSE(unheard.hasDatagram()) << "a refused send must send nothing";

  EXPECT_TRUE(a->send(bNamed, hello));
  a->end();
  EXPECT_FALSE(a->send(bNamed, hello)) << "a dead handle";
}

// ---------------------------------------------------------------- the base, the label

TEST(UdpLinkTest, TheBaseComesFromTheEnvironmentAtEachStart) {
  BaseVariable variable(nullptr);
  EXPECT_EQ(UdpLink::basePortFromEnvironment(), 45700);
  const struct {
    const char* text;
    uint16_t expected;
  } cases[] = {{"abc", 45700},    {"1000", 45700},
               {"65530", 45700},  {"46000", 46000},
               {"", 45700},       {"1024", 1024},
               {"65527", 65527},  {"65528", 45700},
               {"46000x", 45700}, {"0", 45700},
               {" 46000", 45700}, {"+46000", 45700},
               {"-46000", 45700}, {"99999999999999999999", 45700}};
  for (const auto& row : cases) {
    BaseVariable::set(row.text);
    EXPECT_EQ(UdpLink::basePortFromEnvironment(), row.expected) << '"' << row.text << '"';
  }
  // Read afresh: the same process, two values, two bases.
  BaseVariable::set("46000");
  EXPECT_EQ(UdpLink::basePortFromEnvironment(), 46000);
  BaseVariable::set("47000");
  EXPECT_EQ(UdpLink::basePortFromEnvironment(), 47000);
  BaseVariable::set(nullptr);
  EXPECT_EQ(UdpLink::basePortFromEnvironment(), 45700);
}

TEST(UdpLinkTest, MakeUdpLinkStartsOnTheEnvironmentsBaseEachTime) {
  const uint16_t first = freeBase();
  ASSERT_NE(first, 0);
  BaseVariable variable(std::to_string(first).c_str());
  auto a = makeUdpLink();
  ASSERT_TRUE(a);
  EXPECT_EQ(a->port(), first);
  // A second link of the same value takes the next port; a changed value moves the range.
  auto b = makeUdpLink();
  ASSERT_TRUE(b);
  EXPECT_EQ(b->port(), first + 1);
  const uint16_t second = freeBase();
  BaseVariable::set(second);
  auto c = makeUdpLink();
  ASSERT_TRUE(c);
  EXPECT_EQ(c->port(), second);
}

TEST(UdpLinkTest, ABaseThatLeavesNoRoomIsRefused) {
  fakelog::clearLines();
  EXPECT_EQ(UdpLink::create(0), nullptr);
  EXPECT_EQ(UdpLink::create(65530), nullptr);
  EXPECT_TRUE(fakelog::anyLine("ERR LINK: udp base port"));
}

TEST(UdpLinkTest, TheHostLabelIsSimulatorAndThePort) {
  Range range;
  ASSERT_NE(range.base, 0);
  auto a = mustCreate(range.base);
  ASSERT_TRUE(a);
  EXPECT_EQ(labelText(*a), "Simulator " + std::to_string(range.base));
  EXPECT_LE(a->hostLabel().size(), GameCore::Protocol::MAX_HOST_LABEL_BYTES);
  EXPECT_EQ(a->hostLabel().size(), 15u) << "five digits";
  auto b = mustCreate(range.base);
  ASSERT_TRUE(b);
  EXPECT_EQ(labelText(*b), "Simulator " + std::to_string(range.base + 1));
}

// ---------------------------------------------------------------- a lobby and a round

TEST(UdpLinkTest, TwoSessionsOverUdpLinksListJoinStartAndPlayARound) {
  Range range;
  ASSERT_NE(range.base, 0);
  Duo d;  // its rig: the clock, the random source, the game; the FakeLink pair stays unused
  auto hostLink = mustCreate(range.base);
  auto guestLink = mustCreate(range.base);
  ASSERT_TRUE(hostLink && guestLink);
  const std::string label = labelText(*hostLink);
  d.host = NearbySession::openHost(std::move(hostLink), d.clock, d.random, d.game,
                                   GameCore::LobbyTest::bytesOf(label.c_str()), fastSettings(), fastOptions());
  d.guest = NearbySession::openGuest(std::move(guestLink), d.clock, d.game, fastSettings(), fastOptions());
  ASSERT_NE(d.host, nullptr);
  ASSERT_NE(d.guest, nullptr);

  // Listed: the guest sees the host with its label.
  ASSERT_TRUE(waitFor([&] { return d.guest->lobby().guest()->hostCount() >= 1; }));
  {
    auto lobby = d.guest->lobby();
    const GameCore::HostEntry& entry = lobby.guest()->host(0);
    EXPECT_EQ(std::string(entry.label.begin(), entry.label.begin() + entry.labelLen), label);
  }
  ASSERT_TRUE(d.join());
  ASSERT_TRUE(d.start());

  // A STATE each way: the host starts the round, the two take turns, and both end on the same final state.
  MatchDriver a(*d.host, 1), b(*d.guest, 2);
  ASSERT_TRUE(a.startRound());
  ASSERT_TRUE(waitFor(
      [&] {
        a.step();
        b.step();
        return a.match.status().over && b.match.status().over && sameState(a.match, b.match);
      },
      20000));
  EXPECT_GT(a.moves, 0u);
  EXPECT_GT(b.moves, 0u) << "a STATE went from the guest to the host";
  EXPECT_FALSE(a.ended);
  EXPECT_FALSE(b.ended);
  EXPECT_GE(std::count(b.verdicts.begin(), b.verdicts.end(), GameCore::Adoption::RoundStarted), 1)
      << "a STATE went from the host to the guest";
}

}  // namespace
