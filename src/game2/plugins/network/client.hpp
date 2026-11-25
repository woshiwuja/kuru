#pragma once
#include "../character/character.hpp" // Player, PendingMesh
#include "network.hpp"
#include <memory>

struct RemotePlayer {
  uint32_t id;
};

// A "Network" window to pick the server and connect. While connected it sends
// the local Player's position and mirrors everyone else as RemotePlayer
// entities drawn with `look` (CharacterPlugin::start turns it into a mesh).
struct NetClientPlugin : public KR::Plugin {
  std::string ip = "127.0.0.1";
  int port = NET_PORT;
  std::string error;

  std::unique_ptr<UdpSocket> socket; // null: disconnected
  sockaddr_in server{};
  uint32_t myId = 0;
  std::chrono::steady_clock::time_point lastHeard{};

  PendingMesh look; // copied onto every new remote player
  std::unordered_map<uint32_t, entt::entity> remotes;

  explicit NetClientPlugin(PendingMesh look) : look(std::move(look)) {}

  bool connected() const {
    return socket && myId != 0 &&
           std::chrono::steady_clock::now() - lastHeard < NET_TIMEOUT;
  }

  void connect() {
    error.clear();
    try {
      if (port <= 0 || port > 65535) {
        throw std::runtime_error("port out of range");
      }
      server = netAddress(ip, static_cast<uint16_t>(port));
      socket = std::make_unique<UdpSocket>(0);
      myId = 0;
      lastHeard = std::chrono::steady_clock::now(); // grace period to answer
    } catch (const std::exception &e) {
      error = e.what();
      socket = nullptr;
    }
  }

  void disconnect(entt::registry &reg) {
    socket = nullptr;
    myId = 0;
    for (const auto &[id, e] : remotes) {
      renderer(reg).despawn(reg, e);
    }
    remotes.clear();
  }

  void update(entt::registry &reg) override {
    ui(reg);
    if (!socket) {
      return;
    }
    if (std::chrono::steady_clock::now() - lastHeard > NET_TIMEOUT) {
      error = "server not responding";
      disconnect(reg);
      return;
    }
    // ponytail: sends every frame; throttle to a fixed tick if bandwidth matters.
    for (auto [e, player, t] : reg.view<Player, Transform>().each()) {
      const NetPositionPacket packet{glm::vec3(t.position)};
      socket->send(server, &packet, sizeof packet);
    }
    receive(reg);
  }

  void ui(entt::registry &reg) {
    ImGui::Begin("Network");
    if (socket) {
      if (connected()) {
        ImGui::Text("Connected to %s:%d as #%u", ip.c_str(), port, myId);
        ImGui::Text("%zu other players", remotes.size());
      } else {
        ImGui::Text("Connecting to %s:%d...", ip.c_str(), port);
      }
      if (ImGui::Button("Disconnect")) {
        disconnect(reg);
      }
    } else {
      ImGui::InputText("IP", &ip);
      ImGui::InputInt("Port", &port);
      if (ImGui::Button("Connect")) {
        connect();
      }
    }
    if (!error.empty()) {
      ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", error.c_str());
    }
    ImGui::End();
  }

  void receive(entt::registry &reg) {
    // Only the newest snapshot matters, older ones are stale.
    std::vector<char> latest;
    std::vector<char> buffer(sizeof(NetSnapshotHeader) +
                             NET_MAX_PLAYERS * sizeof(NetSnapshotEntry));
    sockaddr_in from{};
    ssize_t n = 0;
    while ((n = socket->receive(from, buffer.data(), buffer.size())) > 0) {
      if (from.sin_addr.s_addr == server.sin_addr.s_addr &&
          from.sin_port == server.sin_port) {
        latest.assign(buffer.begin(), buffer.begin() + n);
      }
    }
    if (latest.size() < sizeof(NetSnapshotHeader)) {
      return;
    }
    const auto *header =
        reinterpret_cast<const NetSnapshotHeader *>(latest.data());
    if (header->count > NET_MAX_PLAYERS ||
        latest.size() != sizeof(NetSnapshotHeader) +
                             header->count * sizeof(NetSnapshotEntry)) {
      return; // malformed
    }
    lastHeard = std::chrono::steady_clock::now();
    myId = header->yourId;
    const auto *entries = reinterpret_cast<const NetSnapshotEntry *>(
        latest.data() + sizeof(NetSnapshotHeader));

    std::unordered_map<uint32_t, entt::entity> seen;
    for (uint32_t i = 0; i < header->count; i++) {
      const NetSnapshotEntry &entry = entries[i];
      if (entry.id == myId) {
        continue;
      }
      entt::entity e = entt::null;
      if (auto it = remotes.find(entry.id); it != remotes.end()) {
        e = it->second;
      } else {
        e = reg.create();
        reg.emplace<RemotePlayer>(e, entry.id);
        reg.emplace<Transform>(e);
        reg.emplace<PendingMesh>(e, look);
      }
      reg.get<Transform>(e).position = {entry.position.x, entry.position.y,
                                         entry.position.z};
      seen[entry.id] = e;
    }
    for (const auto &[id, e] : remotes) {
      if (!seen.contains(id)) {
        renderer(reg).despawn(reg, e);
      }
    }
    remotes = std::move(seen);
  }
};
