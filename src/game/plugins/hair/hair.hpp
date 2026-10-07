#pragma once
#include "../render/render.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeSystem.h>
#include <Jolt/Physics/Hair/Hair.h>
#include <Jolt/Physics/Hair/HairSettings.h>
#include <Jolt/Physics/Hair/HairShaders.h>
#include <Kuru.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

using namespace KR;

// A groom (Cem Yuksel's .hair format) to grow on this entity's scalp: the
// model node named "scalp", either this entity or one of its children.
// HairPlugin consumes it once that node exists, like ModelPath.
struct HairPath {
  std::string path;
};

// A simulated groom, on its own entity under the scalp. Drawn as lines in
// the model's space, so it is a ModelNode of the model's root and follows
// its Transform; Jolt's Hair is moved to the same place every frame.
struct HairInstance {
  entt::entity scalp = entt::null;
  // Who had the HairPath, and its file: where "Rebuild" puts it back.
  entt::entity owner = entt::null;
  std::string path;
  JPH::Ref<JPH::HairSettings> settings;
  std::unique_ptr<JPH::Hair> hair;
  // One line mesh per frame in flight: positions are rewritten every frame
  // through `mapped`, never into one the GPU may still be reading.
  std::array<std::shared_ptr<Mesh>, MAX_FRAMES_IN_FLIGHT> lines;
  std::array<Vertex *, MAX_FRAMES_IN_FLIGHT> mapped{};
  std::vector<Vertex> vertices; // colours fixed, positions refreshed
};

struct HairPlugin : public Plugin {
  HairPlugin() { registerComponent<HairPath>(); }

  // Jolt runs hair as compute shaders; this build has its CPU backend only
  // (JPH_USE_VK is off in CMakeLists.txt).
  JPH::Ref<JPH::ComputeSystem> computeSystem;
  JPH::Ref<JPH::ComputeQueue> computeQueue;
  JPH::Ref<JPH::HairShaders> shaders;

  // Every groom's Jolt material, copied into its settings each update, so
  // edits apply live (as in the sample's menu). The init-only fields
  // (gravity preload, simulated fraction) take a Rebuild.
  JPH::HairSettings::Material material = defaultMaterial();
  uint32_t iterationsPerSecond = JPH::HairSettings::cDefaultIterationsPerSecond;

  // Baked into a groom when it's built: "Rebuild" regrows every groom.
  // ponytail: one set for all grooms; per-groom settings on HairPath if
  // characters need different hair.
  uint32_t maxStrands = 10000; // of the file's, spread evenly across it
  uint32_t verticesPerStrand = 32;
  glm::vec3 color{0.22f, 0.13f, 0.07f};
  bool rebuildRequested = false;

  // The sample's defaults, except collision: off, because the scalp sits
  // inside the character's own capsule, which would push every strand out.
  // Needs a head-shaped body to collide with, like the sample's hulls.
  static JPH::HairSettings::Material defaultMaterial();

  void init(entt::registry &reg) override;
  void start(entt::registry &reg) override;
  void update(entt::registry &reg) override;

  // The groom on `scalp` as a new HairInstance entity; null if the file
  // can't be read.
  entt::entity grow(entt::registry &reg, entt::entity scalp,
                    entt::entity owner, const std::string &path);
  void UI(entt::registry &reg);
};
