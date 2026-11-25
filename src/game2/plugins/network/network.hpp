#pragma once
// Basic UDP networking: every client sends its player position to the server,
// the server sends every client the positions of all players.
// ponytail: POSIX sockets only (like Kuru's forkpty); Windows needs winsock's
// WSAStartup/closesocket/ioctlsocket behind an #ifdef.
// ponytail: raw structs on the wire, so every machine must share endianness
// and float layout (true for x86/ARM); serialize field by field otherwise.
// ponytail: no reliability, ordering or auth, just latest-state-wins.
#include "plugin/plugin.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <glm/glm.hpp>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

constexpr uint16_t NET_PORT = 7777;
constexpr uint32_t NET_MAX_PLAYERS = 64; // keeps a snapshot under ~1KB
constexpr auto NET_TIMEOUT = std::chrono::seconds(2);

// client -> server
struct NetPositionPacket {
  glm::vec3 position;
};
// server -> client: a NetSnapshotHeader followed by `count` NetSnapshotEntry.
struct NetSnapshotHeader {
  uint32_t yourId;
  uint32_t count;
};
struct NetSnapshotEntry {
  uint32_t id;
  glm::vec3 position;
};

// Non-blocking UDP socket bound to `port` (0: any free port).
struct UdpSocket {
  int fd = -1;

  explicit UdpSocket(uint16_t port) {
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
      throw std::runtime_error("socket() failed");
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
      close(fd);
      throw std::runtime_error("bind() failed on port " + std::to_string(port));
    }
  }
  ~UdpSocket() { close(fd); }
  UdpSocket(const UdpSocket &) = delete;
  UdpSocket &operator=(const UdpSocket &) = delete;

  void send(const sockaddr_in &to, const void *data, size_t size) const {
    sendto(fd, data, size, 0, reinterpret_cast<const sockaddr *>(&to),
           sizeof to);
  }
  // Bytes read, or <= 0 when nothing is waiting.
  ssize_t receive(sockaddr_in &from, void *buffer, size_t capacity) const {
    socklen_t len = sizeof from;
    return recvfrom(fd, buffer, capacity, 0, reinterpret_cast<sockaddr *>(&from),
                    &len);
  }
};

inline sockaddr_in netAddress(const std::string &ip, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // ponytail: dotted IPv4 only; getaddrinfo if hostnames are ever needed.
  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
    throw std::runtime_error("not an IPv4 address: " + ip);
  }
  return addr;
}

// Needs nothing but the registry, so it runs in a headless Core.
struct NetServerPlugin : public KR::Plugin {
  struct Client {
    uint32_t id;
    sockaddr_in address;
    glm::vec3 position{0.0f};
    std::chrono::steady_clock::time_point lastSeen;
  };

  UdpSocket socket;
  std::unordered_map<uint64_t, Client> clients; // keyed by ip:port
  uint32_t nextId = 1;

  explicit NetServerPlugin(uint16_t port = NET_PORT) : socket(port) {}

  static uint64_t key(const sockaddr_in &a) {
    return (uint64_t{a.sin_addr.s_addr} << 16) | a.sin_port;
  }

  void update(entt::registry &) override {
    const auto now = std::chrono::steady_clock::now();
    NetPositionPacket packet;
    sockaddr_in from{};
    while (socket.receive(from, &packet, sizeof packet) ==
           static_cast<ssize_t>(sizeof packet)) {
      auto it = clients.find(key(from));
      if (it == clients.end()) {
        if (clients.size() >= NET_MAX_PLAYERS) {
          continue;
        }
        it = clients.emplace(key(from), Client{.id = nextId++, .address = from})
                 .first;
        printf("client %u joined\n", it->second.id);
      }
      it->second.position = packet.position;
      it->second.lastSeen = now;
    }

    std::erase_if(clients, [&](const auto &kv) {
      if (now - kv.second.lastSeen > NET_TIMEOUT) {
        printf("client %u timed out\n", kv.second.id);
        return true;
      }
      return false;
    });

    std::vector<char> buffer(sizeof(NetSnapshotHeader) +
                             clients.size() * sizeof(NetSnapshotEntry));
    auto *entries =
        reinterpret_cast<NetSnapshotEntry *>(buffer.data() + sizeof(NetSnapshotHeader));
    uint32_t i = 0;
    for (const auto &[k, c] : clients) {
      entries[i++] = {c.id, c.position};
    }
    auto *header = reinterpret_cast<NetSnapshotHeader *>(buffer.data());
    header->count = i;
    for (const auto &[k, c] : clients) {
      header->yourId = c.id;
      socket.send(c.address, buffer.data(), buffer.size());
    }
  }
};
