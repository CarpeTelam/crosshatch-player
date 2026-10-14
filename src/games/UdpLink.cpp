#if FREEINK_CAP_GAMES && defined(SIMULATOR)

#include "UdpLink.h"

#include <Logging.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <new>

// Adapted from crossplay 0a1d17f src/apps_local/link/LinkRadio.cpp, its SIMULATOR branch (MIT); the notice is in
// UdpLink.h.

namespace {

using GameCore::PeerHandle;

constexpr uint16_t MAX_BASE_PORT = 65535 - UdpLink::SLOTS;
constexpr uint16_t MIN_ENV_BASE_PORT = 1024;  // below it are the privileged ports

// Handle values are unique across every link instance in the process (ILink.h): each instance takes a tag from here,
// 1..0xFFFF, and puts it above its peer's port. A 4 B mutable static, constant-initialized (AD-2); it exists only in
// the simulator.
constinit std::atomic<uint32_t> nextInstanceTag{0};

uint32_t takeInstanceTag() { return nextInstanceTag.fetch_add(1, std::memory_order_relaxed) % 0xFFFFu + 1; }

sockaddr_in loopbackAddress(const uint16_t port) {
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  return address;
}

bool frameSizeOk(const std::span<const uint8_t> frame) {
  return !frame.empty() && frame.size() <= GameCore::Protocol::MAX_FRAME_BYTES;
}

}  // namespace

uint16_t UdpLink::basePortFromEnvironment() {
  const char* const text = std::getenv(BASE_PORT_ENV);  // each call, never cached (AD-2)
  // A whole number: strtol alone would take leading blanks and a sign.
  if (text == nullptr || *text < '0' || *text > '9') return DEFAULT_BASE_PORT;
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(text, &end, 10);
  // A range under the privileged ports, or one that would run past 65535, is worse than ignoring the override.
  if (errno != 0 || *end != '\0' || parsed < MIN_ENV_BASE_PORT || parsed > MAX_BASE_PORT) return DEFAULT_BASE_PORT;
  return static_cast<uint16_t>(parsed);
}

UdpLink::UdpLink(const int fd, const uint16_t basePort, const uint16_t port, const uint32_t instanceTag)
    : socket_(fd), base_(basePort), port_(port), tag_(instanceTag) {
  char text[24];
  const int written = std::snprintf(text, sizeof(text), "Simulator %u", static_cast<unsigned>(port));
  for (int i = 0; i < written && labelLength_ < sizeof(label_); i++)
    label_[labelLength_++] = static_cast<uint8_t>(text[i]);
}

UdpLink::~UdpLink() { end(); }

std::unique_ptr<UdpLink> UdpLink::create(const uint16_t basePort) {
  if (basePort == 0 || basePort > MAX_BASE_PORT) {
    LOG_ERR("LINK", "udp base port %u leaves no room for %u ports", static_cast<unsigned>(basePort),
            static_cast<unsigned>(SLOTS));
    return nullptr;
  }
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    LOG_ERR("LINK", "could not open a udp socket");
    return nullptr;
  }
  // Deliberately no SO_REUSEADDR: a bind that fails is how a link finds that another instance owns that port.
  size_t slot = 0;
  for (; slot < SLOTS; slot++) {
    const sockaddr_in address = loopbackAddress(static_cast<uint16_t>(basePort + slot));
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) break;
  }
  if (slot == SLOTS) {
    LOG_ERR("LINK", "no free udp port in %u..%u", static_cast<unsigned>(basePort),
            static_cast<unsigned>(basePort + SLOTS - 1));
    ::close(fd);
    return nullptr;
  }
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    LOG_ERR("LINK", "could not make the udp socket non-blocking");
    ::close(fd);
    return nullptr;
  }
  // One object of about 40 B, owned by the returned unique_ptr; the constructor is private, so makeUniqueNoThrow
  // cannot build it. A null return closes the socket.
  const uint16_t port = static_cast<uint16_t>(basePort + slot);
  std::unique_ptr<UdpLink> link(new (std::nothrow) UdpLink(fd, basePort, port, takeInstanceTag()));
  if (!link) {
    LOG_ERR("LINK", "out of memory for a udp link");
    ::close(fd);
    return nullptr;
  }
  LOG_INF("LINK", "udp link up on %u", static_cast<unsigned>(port));
  return link;
}

size_t UdpLink::slotOf(const uint16_t port) const {
  if (port < base_ || port >= base_ + SLOTS) return SLOTS;
  return static_cast<size_t>(port - base_);
}

bool UdpLink::send(const PeerHandle to, const std::span<const uint8_t> frame) {
  if (socket_ < 0 || !to.valid() || !frameSizeOk(frame)) return false;
  const uint32_t value = to.value();
  if ((value >> 16) != tag_) return false;  // another link instance's handle
  const uint16_t port = static_cast<uint16_t>(value & 0xFFFFu);
  const size_t slot = slotOf(port);
  if (slot == SLOTS || port == port_ || (heard_ & (1u << slot)) == 0) return false;  // names no peer heard from
  const sockaddr_in destination = loopbackAddress(port);
  const ssize_t sent = ::sendto(socket_, frame.data(), frame.size(), 0, reinterpret_cast<const sockaddr*>(&destination),
                                sizeof(destination));
  return sent == static_cast<ssize_t>(frame.size());
}

bool UdpLink::broadcast(const std::span<const uint8_t> frame) {
  if (socket_ < 0 || !frameSizeOk(frame)) return false;
  // There is no loopback broadcast worth relying on, so a broadcast is a send to every port of the range but our own.
  bool any = false;
  for (size_t slot = 0; slot < SLOTS; slot++) {
    const uint16_t port = static_cast<uint16_t>(base_ + slot);
    if (port == port_) continue;
    const sockaddr_in destination = loopbackAddress(port);
    const ssize_t sent = ::sendto(socket_, frame.data(), frame.size(), 0,
                                  reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    if (sent == static_cast<ssize_t>(frame.size())) any = true;
  }
  return any;
}

bool UdpLink::receive(PeerHandle& from, const std::span<uint8_t> buffer, size_t& length) {
  if (socket_ < 0) return false;
  for (;;) {
    sockaddr_in source = {};
    iovec chunk = {buffer.data(), buffer.size()};
    msghdr message = {};
    message.msg_name = &source;
    message.msg_namelen = sizeof(source);
    message.msg_iov = &chunk;
    message.msg_iovlen = 1;
    const ssize_t received = ::recvmsg(socket_, &message, 0);
    if (received < 0) {
      if (errno == EINTR) continue;
      return false;  // EAGAIN: nothing waiting
    }
    // Dropped, never truncated: a datagram the buffer could not hold (MSG_TRUNC), an empty one (a frame is 1..max).
    // A datagram over the largest frame goes the same way when the buffer is bigger than a frame (send refuses such
    // sizes).
    if (received == 0 || (message.msg_flags & MSG_TRUNC) != 0 ||
        static_cast<size_t>(received) > GameCore::Protocol::MAX_FRAME_BYTES) {
      continue;
    }
    // The sender is the socket's source address, never anything the payload claims. A port outside the range, or our
    // own, is a stranger.
    if (message.msg_namelen < sizeof(sockaddr_in) || source.sin_family != AF_INET) continue;
    const uint16_t port = ntohs(source.sin_port);
    const size_t slot = slotOf(port);
    if (slot == SLOTS || port == port_) continue;
    heard_ = static_cast<uint8_t>(heard_ | (1u << slot));
    from = PeerHandle::fromValue((tag_ << 16) | port);
    length = static_cast<size_t>(received);
    return true;
  }
}

void UdpLink::end() {
  if (socket_ >= 0) ::close(socket_);
  socket_ = -1;
  heard_ = 0;
}

std::unique_ptr<UdpLink> makeUdpLink() { return UdpLink::create(UdpLink::basePortFromEnvironment()); }

#endif  // FREEINK_CAP_GAMES && SIMULATOR
