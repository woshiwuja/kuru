#include "hair.hpp"
#include "../transform/transform.hpp"
#include "imgui.h"
#include <Jolt/Compute/CPU/ComputeSystemCPU.h>
#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Shaders/HairWrapper.h>
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {

// One joint, never moving: the scalp is a static mesh in the model's space,
// and the whole groom moves with Hair::SetPosition/SetRotation instead.
const JPH::Mat44 identityJoint = JPH::Mat44::sIdentity();

// The strands of a .hair file (https://www.cemyuksel.com/research/hairmodels/),
// in metres, Y up. At most `maxStrands`, taken evenly across the file. Parser
// from Jolt's Samples/Tests/Hair/HairTest.cpp, with bounds checks added.
std::vector<std::vector<glm::vec3>> readHairFile(const std::string &path,
                                                 uint32_t maxStrands) {
  std::ifstream file(assetPath(path), std::ios::binary);
  const std::vector<uint8_t> data{std::istreambuf_iterator<char>(file), {}};
  constexpr size_t headerSize = 128;
  if (data.size() < headerSize || std::memcmp(data.data(), "HAIR", 4) != 0) {
    std::cout << "hair: " << path << " is missing or not a .hair file\n";
    return {};
  }
  uint32_t numStrands = 0, numPoints = 0, features = 0, defaultSegments = 0;
  std::memcpy(&numStrands, &data[4], 4);
  std::memcpy(&numPoints, &data[8], 4);
  std::memcpy(&features, &data[12], 4);
  std::memcpy(&defaultSegments, &data[16], 4);
  if ((features & 0b10) != 0b10) {
    std::cout << "hair: " << path << " has no points\n";
    return {};
  }
  // Bit 0: a segment count per strand follows the header, else every strand
  // has defaultSegments.
  const bool perStrandSegments = (features & 0b01) != 0;
  size_t pointsOffset = headerSize;
  if (perStrandSegments) {
    pointsOffset += size_t(numStrands) * sizeof(uint16_t);
  }
  if (data.size() < pointsOffset + size_t(numPoints) * 12) {
    std::cout << "hair: " << path << " is truncated\n";
    return {};
  }

  uint32_t stride = 1;
  if (maxStrands > 0 && numStrands > maxStrands) {
    stride = (numStrands + maxStrands - 1) / maxStrands;
  }
  std::vector<std::vector<glm::vec3>> strands;
  size_t point = 0;
  for (uint32_t s = 0; s < numStrands; s++) {
    uint32_t segments = defaultSegments;
    if (perStrandSegments) {
      uint16_t n = 0;
      std::memcpy(&n, &data[headerSize + s * sizeof(uint16_t)], 2);
      segments = n;
    }
    if (point + segments + 1 > numPoints) {
      break; // segment counts claim more points than the file has
    }
    if (s % stride == 0 && segments > 0) {
      std::vector<glm::vec3> strand;
      for (uint32_t i = 0; i <= segments; i++) {
        float raw[3];
        std::memcpy(raw, &data[pointsOffset + (point + i) * 12], 12);
        // Tenths of an inch, Z up: the sample's swizzle and scale.
        strand.push_back(0.00254f * glm::vec3(raw[1], raw[2], raw[0]));
      }
      strands.push_back(std::move(strand));
    }
    point += segments + 1;
  }
  return strands;
}

// Moves the strands so their roots cover the scalp: uniform scale from the
// XZ extents, centred in XZ, tops aligned.
// ponytail: bounding boxes only, right for a scalp shaped like the groom's
// head; give HairPath an offset/scale/yaw if a model needs hand-fitting.
void fitToScalp(std::vector<std::vector<glm::vec3>> &strands,
                const Mesh &scalp) {
  glm::vec3 rootLo(std::numeric_limits<float>::max()), rootHi(-rootLo);
  for (const auto &strand : strands) {
    rootLo = glm::min(rootLo, strand.front());
    rootHi = glm::max(rootHi, strand.front());
  }
  const glm::vec3 rootSize = rootHi - rootLo;
  const glm::vec3 scalpSize = scalp.boundsMax - scalp.boundsMin;
  float scale = 1.0f;
  if (rootSize.x + rootSize.z > 0.0f) {
    scale = (scalpSize.x + scalpSize.z) / (rootSize.x + rootSize.z);
  }
  const glm::vec3 from{(rootLo.x + rootHi.x) * 0.5f, rootHi.y,
                       (rootLo.z + rootHi.z) * 0.5f};
  const glm::vec3 to{(scalp.boundsMin.x + scalp.boundsMax.x) * 0.5f,
                     scalp.boundsMax.y,
                     (scalp.boundsMin.z + scalp.boundsMax.z) * 0.5f};
  for (auto &strand : strands) {
    for (glm::vec3 &p : strand) {
      p = (p - from) * scale + to;
    }
  }
}

// A line mesh whose vertex buffer stays mapped for per-frame rewrites.
std::shared_ptr<Mesh> lineMesh(const std::vector<Vertex> &vertices,
                               const std::vector<uint32_t> &indices,
                               Vertex *&mapped) {
  auto mesh = std::make_shared<Mesh>();
  // Index buffer and bounds; the device-local vertex buffer is replaced.
  mesh->upload(vertices, indices);
  const vk::DeviceSize size = sizeof(Vertex) * vertices.size();
  createBuffer(size, vk::BufferUsageFlagBits::eVertexBuffer,
               vk::MemoryPropertyFlagBits::eHostVisible |
                   vk::MemoryPropertyFlagBits::eHostCoherent,
               mesh->vertexBuffer, mesh->vertexBufferMemory);
  mapped = static_cast<Vertex *>(mesh->vertexBufferMemory.mapMemory(0, size));
  std::memcpy(mapped, vertices.data(), size);
  return mesh;
}

// The scalp node of `e`'s model: `e` itself or one of its children. Null
// until the model has spawned its nodes.
entt::entity findScalp(entt::registry &reg, entt::entity e) {
  const auto isScalp = [&reg](entt::entity x) {
    const auto *node = reg.try_get<ModelNode>(x);
    return node != nullptr && std::strcmp(node->name, "scalp") == 0;
  };
  if (isScalp(e)) {
    return e;
  }
  entt::entity found = entt::null;
  if (reg.all_of<Relationship>(e)) {
    each_child(reg, e, [&](entt::entity child) {
      if (isScalp(child)) {
        found = child;
      }
    });
  }
  return found;
}

// The line meshes' GPU buffers outlive the entity until no frame in flight
// can still be drawing them.
void retireLines(entt::registry &reg, entt::entity e) {
  auto &render = renderer(reg);
  for (auto &mesh : reg.get<HairInstance>(e).lines) {
    RenderPlugin::Retired old{.frame = render.frameCount};
    old.mesh = mesh;
    render.retired.push_back(std::move(old));
  }
}

} // namespace

JPH::HairSettings::Material HairPlugin::defaultMaterial() {
  JPH::HairSettings::Material m;
  m.mEnableCollision = false;
  m.mEnableLRA = true;
  m.mLinearDamping = 2.0f;
  m.mAngularDamping = 2.0f;
  m.mFriction = 0.2f;
  m.mMaxLinearVelocity = 10.0f;
  m.mMaxAngularVelocity = 50.0f;
  m.mGravityFactor = {0.1f, 1.0f, 0.2f, 0.8f};
  m.mGravityPreloadFactor = 1.0f;
  m.mBendCompliance = 1.0e-7f;
  m.mStretchCompliance = 1.0e-8f;
  m.mInertiaMultiplier = 10.0f;
  m.mHairRadius = {0.001f, 0.001f};
  m.mWorldTransformInfluence = {0.0f, 1.0f};
  m.mGridVelocityFactor = {0.0f, 0.0f};
  m.mGridDensityForceFactor = 0.0f;
  m.mGlobalPose = {0.01f, 0.0f, 0.0f, 0.3f};
  m.mSkinGlobalPose = {1.0f, 0.0f, 0.0f, 0.1f};
  m.mSimulationStrandsFraction = 0.01f;
  return m;
}

namespace {
// False if any shader failed to load: HairShaders::Init leaves those null
// rather than reporting it.
bool allLoaded(const JPH::HairShaders &s) {
  for (const JPH::ComputeShader *shader :
       {s.mTeleportCS.GetPtr(), s.mApplyDeltaTransformCS.GetPtr(),
        s.mSkinVerticesCS.GetPtr(), s.mSkinRootsCS.GetPtr(),
        s.mApplyGlobalPoseCS.GetPtr(), s.mCalculateCollisionPlanesCS.GetPtr(),
        s.mGridClearCS.GetPtr(), s.mGridAccumulateCS.GetPtr(),
        s.mGridNormalizeCS.GetPtr(), s.mIntegrateCS.GetPtr(),
        s.mUpdateRootsCS.GetPtr(), s.mUpdateStrandsCS.GetPtr(),
        s.mUpdateVelocityCS.GetPtr(), s.mUpdateVelocityIntegrateCS.GetPtr(),
        s.mCalculateRenderPositionsCS.GetPtr()}) {
    if (shader == nullptr) {
      return false;
    }
  }
  return true;
}

// Jolt's SPIR-V, compiled with its library and copied next to the binary
// (see CMakeLists.txt).
bool loadJoltShader(const char *name, JPH::Array<JPH::uint8> &out,
                    JPH::String &error) {
  const std::string path = assetPath(std::string("shaders/jolt/") + name);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = ("missing " + path).c_str();
    return false;
  }
  out.resize(static_cast<size_t>(file.tellg()));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(out.data()),
            static_cast<std::streamsize>(out.size()));
  return true;
}
} // namespace

void HairPlugin::init(entt::registry &reg) {
  // Vulkan compute (its own device, apart from the renderer's) when it comes
  // up with every shader; the CPU backend otherwise, which runs the same
  // shaders compiled as C++ once they're registered.
  JPH::ComputeSystemResult vk = JPH::CreateComputeSystemVK();
  if (vk.IsValid()) {
    computeSystem = vk.Get();
    computeSystem->mShaderLoader = loadJoltShader;
    shaders = new JPH::HairShaders;
    shaders->Init(computeSystem);
    JPH::ComputeQueueResult queue = computeSystem->CreateComputeQueue();
    if (allLoaded(*shaders) && queue.IsValid()) {
      computeQueue = queue.Get();
      backend = "GPU (Vulkan)";
    } else {
      std::cout << "hair: Vulkan compute without its shaders, using the CPU\n";
      shaders = nullptr;
      computeSystem = nullptr;
    }
  } else {
    std::cout << "hair: no Vulkan compute (" << vk.GetError()
              << "), using the CPU\n";
  }
  if (computeSystem == nullptr) {
    computeSystem = JPH::CreateComputeSystemCPU().Get();
    JPH::HairRegisterShaders(
        static_cast<JPH::ComputeSystemCPU *>(computeSystem.GetPtr()));
    computeQueue = computeSystem->CreateComputeQueue().Get();
    shaders = new JPH::HairShaders;
    shaders->Init(computeSystem);
    backend = "CPU";
  }
  reg.on_destroy<HairInstance>().connect<&retireLines>();
}

void HairPlugin::start(entt::registry &reg) {
  // This frame's copy of each groom's lines, before RenderPlugin records it.
  const uint32_t frameIndex = reg.ctx().get<FrameContext>().frameIndex;
  for (auto [e, instance, meshRef] : reg.view<HairInstance, MeshRef>().each()) {
    meshRef.mesh = instance.lines[frameIndex];
  }

  // A groom whose scalp is gone (model swapped) goes too. On Rebuild every
  // groom, and from the dropdowns the chosen ones, go with their HairPath put
  // back so the loop below grows them again. Spawning and despawning only
  // here: see PhysicsPlugin::start.
  if (rebuildRequested) {
    grooms.clear(); // built with the old settings
    for (auto [e, instance] : reg.view<HairInstance>().each()) {
      regrow.emplace_back(e, instance.path);
    }
  }
  rebuildRequested = false;
  std::vector<entt::entity> bald;
  for (auto [e, instance] : reg.view<HairInstance>().each()) {
    if (!reg.valid(instance.scalp)) {
      bald.push_back(e);
    }
  }
  for (const auto &[e, path] : regrow) {
    if (!reg.valid(e) || !reg.all_of<HairInstance>(e)) {
      continue; // gone, or already queued once
    }
    const entt::entity owner = reg.get<HairInstance>(e).owner;
    if (reg.valid(owner)) {
      reg.emplace_or_replace<HairPath>(owner, HairPath{path});
    }
    bald.push_back(e);
  }
  regrow.clear();
  std::sort(bald.begin(), bald.end());
  bald.erase(std::unique(bald.begin(), bald.end()), bald.end());
  for (entt::entity e : bald) {
    renderer(reg).despawn(reg, e);
  }

  auto pending = reg.view<HairPath>();
  const std::vector<entt::entity> owners(pending.begin(), pending.end());
  for (entt::entity e : owners) {
    const entt::entity scalp = findScalp(reg, e);
    if (scalp == entt::null) {
      continue; // its model hasn't spawned yet
    }
    grow(reg, scalp, e, reg.get<HairPath>(e).path);
    reg.remove<HairPath>(e);
  }
}

// ponytail: a new groom file is parsed and initialised synchronously, a
// hitch the frame it first appears; an async load like NavigationPlugin's if
// that starts to matter.
JPH::Ref<JPH::HairSettings> HairPlugin::groomSettings(const std::string &path,
                                                      const Mesh &scalp) {
  const auto key = std::make_pair(path, &scalp);
  if (const auto it = grooms.find(key); it != grooms.end()) {
    return it->second;
  }
  std::vector<std::vector<glm::vec3>> strands = readHairFile(path, maxStrands);
  if (strands.empty() || scalp.vertices.empty()) {
    return nullptr;
  }
  fitToScalp(strands, scalp);

  JPH::Array<JPH::HairSettings::SVertex> vertices;
  JPH::Array<JPH::HairSettings::SStrand> simStrands;
  for (const auto &strand : strands) {
    const uint32_t first = uint32_t(vertices.size());
    for (size_t i = 0; i < strand.size(); i++) {
      // The root is pinned to the scalp.
      float invMass = 1.0f;
      if (i == 0) {
        invMass = 0.0f;
      }
      vertices.push_back(JPH::HairSettings::SVertex(
          JPH::Float3(strand[i].x, strand[i].y, strand[i].z), invMass));
    }
    simStrands.push_back(
        JPH::HairSettings::SStrand(first, uint32_t(vertices.size()), 0));
  }
  if (verticesPerStrand > 1) {
    JPH::HairSettings::sResample(vertices, simStrands, verticesPerStrand);
  }

  JPH::Ref<JPH::HairSettings> settings = new JPH::HairSettings;
  for (const Vertex &v : scalp.vertices) {
    settings->mScalpVertices.push_back(JPH::Float3(v.pos.x, v.pos.y, v.pos.z));
  }
  for (size_t i = 0; i + 2 < scalp.indices.size(); i += 3) {
    settings->mScalpTriangles.push_back(JPH::IndexedTriangleNoMaterial(
        scalp.indices[i], scalp.indices[i + 1], scalp.indices[i + 2]));
  }
  settings->mScalpInverseBindPose.push_back(identityJoint);
  JPH::HairSettings::SkinWeight weight;
  weight.mJointIdx = 0;
  weight.mWeight = 1.0f;
  settings->mScalpSkinWeights.resize(scalp.vertices.size(), weight);
  settings->mScalpNumSkinWeightsPerVertex = 1;

  settings->mMaterials.push_back(material);
  settings->mNumIterationsPerSecond = iterationsPerSecond;
  settings->mSimulationBoundsPadding = JPH::Vec3::sReplicate(0.1f);
  // In the groom's space. Characters only ever turn about Y, which leaves
  // gravity as it is, so one groom fits every character sharing it.
  settings->mInitialGravity =
      Core::get()->physicsManager->system.GetGravity();
  settings->InitRenderAndSimulationStrands(vertices, simStrands);
  float maxDistSq = 0.0f;
  settings->Init(maxDistSq);
  std::cout << "hair: " << path << ", " << strands.size() << " strands ("
            << settings->mSimStrands.size() << " simulated), roots up to "
            << std::sqrt(maxDistSq) << " from the scalp\n";
  settings->InitCompute(computeSystem);
  grooms.emplace(key, settings);
  return settings;
}

entt::entity HairPlugin::grow(entt::registry &reg, entt::entity scalp,
                              entt::entity owner, const std::string &path) {
  JPH::Ref<JPH::HairSettings> settings =
      groomSettings(path, *reg.get<MeshRef>(scalp).mesh);
  if (settings == nullptr) {
    return entt::null;
  }

  // The model's root: the groom lives in its space and follows its Transform.
  entt::entity root = scalp;
  if (const auto *node = reg.try_get<ModelNode>(scalp)) {
    root = node->root;
  }
  Transform transform;
  if (const auto *t = reg.try_get<Transform>(root)) {
    transform = *t;
  }
  const glm::quat r = glm::normalize(transform.rotation);
  const JPH::Quat rotation(r.x, r.y, r.z, r.w);
  const JPH::RVec3 position(transform.position.x, transform.position.y,
                            transform.position.z);
  auto &physics = Core::get()->physicsManager->system;

  HairInstance instance{
      .scalp = scalp, .owner = owner, .path = path, .settings = settings};
  instance.hair = std::make_unique<JPH::Hair>(settings.GetPtr(), position,
                                              rotation, MOVING);
  instance.hair->Init(computeSystem);
  instance.hair->Update(0.0f, identityJoint, &identityJoint, physics, *shaders,
                        computeSystem, computeQueue);
  computeQueue->ExecuteAndWait();
  instance.hair->ReadBackGPUState(computeQueue);

  // Lines: one vertex per render vertex, a segment between neighbours of a
  // strand. Each strand a slightly different shade, or the groom reads as
  // one flat blob (the line pipeline is unlit).
  instance.vertices.resize(settings->mRenderVertices.size());
  std::vector<uint32_t> indices;
  uint32_t seed = 12345;
  instance.hair->LockReadBackBuffers();
  const JPH::Float3 *positions = instance.hair->GetRenderPositions();
  for (const JPH::HairSettings::RStrand &strand : settings->mRenderStrands) {
    seed = seed * 1664525u + 1013904223u;
    const float shade = 0.7f + 0.6f * float(seed >> 8) / float(1u << 24);
    for (uint32_t v = strand.mStartVtx; v < strand.mEndVtx; v++) {
      Vertex &vertex = instance.vertices[v];
      vertex.pos = {positions[v].x, positions[v].y, positions[v].z};
      vertex.color = color * shade;
      vertex.normal = {0.0f, 1.0f, 0.0f};
      if (v + 1 < strand.mEndVtx) {
        indices.push_back(v);
        indices.push_back(v + 1);
      }
    }
  }
  instance.hair->UnlockReadBackBuffers();
  for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
    instance.lines[f] = lineMesh(instance.vertices, indices, instance.mapped[f]);
  }

  const entt::entity e = reg.create();
  renderer(reg).spawn(reg, e, instance.lines[0], getTexture(reg, ""),
                      {2.0f, 1.0f, 0.0f, 0.0f}, transform);
  reg.emplace<DebugMesh>(e);
  reg.emplace<LineMesh>(e);
  auto &node = reg.emplace<ModelNode>(e, ModelNode{.root = root});
  std::snprintf(node.name, sizeof(node.name), "hair");
  ::attach(reg, e, scalp);
  reg.emplace<HairInstance>(e, std::move(instance));
  return e;
}

// After PhysicsPlugin (bodies moved) and RenderPlugin (this entity's
// Transform copied from the root, its draw recorded: the buffer is only read
// at submit).
void HairPlugin::update(entt::registry &reg) {
  UI(reg);
  const float dt = Core::get()->deltaTime();
  const uint32_t frameIndex = reg.ctx().get<FrameContext>().frameIndex;
  auto &physics = Core::get()->physicsManager->system;
  auto active = reg.view<HairInstance, Transform>();

  // Every groom's dispatches go into the queue, then one run executes them
  // all, instead of a run per character.
  for (auto [e, instance, t] : active.each()) {
    instance.settings->mMaterials[0] = material;
    instance.settings->mNumIterationsPerSecond = iterationsPerSecond;
    const glm::quat r = glm::normalize(t.rotation);
    instance.hair->SetPosition(
        JPH::RVec3(t.position.x, t.position.y, t.position.z));
    instance.hair->SetRotation(JPH::Quat(r.x, r.y, r.z, r.w));
    if (dt > 0.0f) {
      instance.hair->Update(dt, identityJoint, &identityJoint, physics,
                            *shaders, computeSystem, computeQueue);
    }
  }
  // Paused: no step, but the pose is still written below, or the two
  // buffers would alternate between the last two poses.
  if (dt > 0.0f && active.begin() != active.end()) {
    computeQueue->ExecuteAndWait();
    for (auto [e, instance, t] : active.each()) {
      instance.hair->ReadBackGPUState(computeQueue);
    }
  }
  for (auto [e, instance, t] : active.each()) {
    instance.hair->LockReadBackBuffers();
    const JPH::Float3 *positions = instance.hair->GetRenderPositions();
    for (size_t v = 0; v < instance.vertices.size(); v++) {
      instance.vertices[v].pos = {positions[v].x, positions[v].y,
                                  positions[v].z};
    }
    instance.hair->UnlockReadBackBuffers();
    std::memcpy(instance.mapped[frameIndex], instance.vertices.data(),
                sizeof(Vertex) * instance.vertices.size());
  }
}

namespace {
// A Gradient's value along the strand: min at the root end, max at the tip,
// blending between the two fractions of its length.
void gradient(const char *label, JPH::HairSettings::Gradient &g, float max) {
  using namespace ImGui;
  if (TreeNode(label)) {
    SliderFloat("root", &g.mMin, 0.0f, max, "%.4f");
    SliderFloat("tip", &g.mMax, 0.0f, max, "%.4f");
    SliderFloat("from", &g.mMinFraction, 0.0f, g.mMaxFraction - 0.001f);
    SliderFloat("to", &g.mMaxFraction, g.mMinFraction + 0.001f, 1.0f);
    TreePop();
  }
}

// A compliance as its base-10 exponent: the useful range spans ten decades.
void compliance(const char *label, float &value) {
  float exponent = std::log10(std::max(value, 1.0e-10f));
  if (ImGui::SliderFloat(label, &exponent, -10.0f, 0.0f, "10^%.2f")) {
    value = std::pow(10.0f, exponent);
  }
}

// A combo over the .hair files in models/hair; true with `chosen` set when
// the user picks one other than `current`.
bool hairFileCombo(const char *label, const std::string &current,
                   std::string &chosen) {
  using namespace ImGui;
  bool picked = false;
  if (BeginCombo(label, current.c_str())) {
    namespace fs = std::filesystem;
    std::error_code error; // no folder: an empty list, not an exception
    for (const auto &entry :
         fs::directory_iterator(assetPath("models/hair"), error)) {
      if (entry.path().extension() != ".hair") {
        continue;
      }
      const std::string path = "models/hair/" + entry.path().filename().string();
      if (Selectable(path.c_str(), path == current) && path != current) {
        chosen = path;
        picked = true;
      }
    }
    EndCombo();
  }
  return picked;
}
} // namespace

void HairPlugin::UI(entt::registry &reg) {
  using namespace ImGui;
  Begin("Hair");
  auto instances = reg.view<HairInstance>();
  size_t renderVertices = 0;
  for (auto [e, instance] : instances.each()) {
    renderVertices += instance.vertices.size();
  }
  Text("simulated on the %s", backend);
  Text("%zu grooms, %zu render vertices a frame",
       static_cast<size_t>(std::distance(instances.begin(), instances.end())),
       renderVertices);
  std::string chosen;
  std::string current = "(per groom)";
  if (instances.begin() != instances.end()) {
    current = instances.get<HairInstance>(*instances.begin()).path;
  }
  if (hairFileCombo("file (all)", current, chosen)) {
    for (auto [e, instance] : instances.each()) {
      regrow.emplace_back(e, chosen);
    }
  }
  if (TreeNode("grooms")) {
    for (auto [e, instance] : instances.each()) {
      PushID(static_cast<int>(entt::to_integral(e)));
      Text("#%u: %zu strands (%zu simulated)", entt::to_integral(e),
           instance.settings->mRenderStrands.size(),
           instance.settings->mSimStrands.size());
      if (hairFileCombo("file", instance.path, chosen)) {
        regrow.emplace_back(e, chosen);
      }
      PopID();
    }
    TreePop();
  }

  SeparatorText("simulation");
  Checkbox("collision", &material.mEnableCollision);
  SetItemTooltip("Off by default: the character's capsule encloses the scalp "
                 "and would push the hair out");
  Checkbox("long range attachments", &material.mEnableLRA);
  int iterations = static_cast<int>(iterationsPerSecond);
  if (SliderInt("iterations / s", &iterations, 30, 960)) {
    iterationsPerSecond = static_cast<uint32_t>(iterations);
  }
  compliance("bend compliance", material.mBendCompliance);
  compliance("stretch compliance", material.mStretchCompliance);
  SliderFloat("inertia multiplier", &material.mInertiaMultiplier, 1.0f, 100.0f);
  SliderFloat("linear damping", &material.mLinearDamping, 0.0f, 5.0f);
  SliderFloat("angular damping", &material.mAngularDamping, 0.0f, 5.0f);
  SliderFloat("friction", &material.mFriction, 0.0f, 1.0f);
  SliderFloat("max linear velocity", &material.mMaxLinearVelocity, 0.01f, 10.0f);
  SliderFloat("max angular velocity", &material.mMaxAngularVelocity, 0.01f,
              50.0f);
  SliderFloat("grid density force", &material.mGridDensityForceFactor, 0.0f,
              10.0f);
  gradient("gravity factor", material.mGravityFactor, 1.0f);
  gradient("world transform influence", material.mWorldTransformInfluence,
           1.0f);
  gradient("grid velocity factor", material.mGridVelocityFactor, 1.0f);
  gradient("global pose", material.mGlobalPose, 1.0f);
  gradient("skin global pose", material.mSkinGlobalPose, 1.0f);
  gradient("hair radius", material.mHairRadius, 0.01f);
  if (Button("Reset simulation")) {
    const JPH::HairSettings::Material defaults = defaultMaterial();
    const float preload = material.mGravityPreloadFactor;
    const float fraction = material.mSimulationStrandsFraction;
    material = defaults;
    material.mGravityPreloadFactor = preload;
    material.mSimulationStrandsFraction = fraction;
    iterationsPerSecond = defaultIterationsPerSecond;
  }

  SeparatorText("groom (applies on Rebuild)");
  int strands = static_cast<int>(maxStrands);
  if (SliderInt("strands", &strands, 100, 50000)) {
    maxStrands = static_cast<uint32_t>(strands);
  }
  SetItemTooltip("Every strand is drawn, and its render positions computed on "
                 "the CPU each frame: cost grows with this");
  float simulated = 100.0f * material.mSimulationStrandsFraction;
  if (SliderFloat("simulated %", &simulated, 1.0f, 100.0f, "%.0f%%")) {
    material.mSimulationStrandsFraction = simulated / 100.0f;
  }
  int points = static_cast<int>(verticesPerStrand);
  if (SliderInt("points per strand", &points, 2, 64)) {
    verticesPerStrand = static_cast<uint32_t>(points);
  }
  SliderFloat("gravity preload", &material.mGravityPreloadFactor, 0.0f, 1.0f);
  ColorEdit3("color", &color.x);
  if (Button("Rebuild")) {
    rebuildRequested = true;
  }
  End();
}
