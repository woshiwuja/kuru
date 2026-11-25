#pragma once
#include "../camera/camera.hpp"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "entt/entity/fwd.hpp"
#include "physics/raycast.hpp"
#include "plugin/default_plugins/model/model.hpp"
#include <Kuru.h>
#include <glm/gtc/quaternion.hpp>

using namespace KR;

// WASD walks it relative to the camera, Space jumps.
struct Player {
  float speed = 5.0f;     // m/s
  float jumpSpeed = 5.0f; // m/s, straight up
};

// The mesh an entity gets in start(): RenderPlugin isn't up during init().
struct PendingMesh {
  std::shared_ptr<Mesh> mesh;
  glm::vec4 color{1.0f};
};

struct CharacterPlugin : public KR::Plugin {
  static constexpr float halfHeight = 0.5f; // of the capsule's cylinder
  static constexpr float radius = 0.4f;
  static constexpr float floorSize = 40.0f;

  CharacterPlugin() { registerComponent<Player>(); }

  void init(entt::registry &reg) override {
    auto light = reg.create();
    reg.emplace<DirectionalLight>(light);
    reg.emplace<Transform>(light);

    // The quad is drawn, a thin static box under it is what you stand on.
    auto floor = reg.create();
    reg.emplace<Transform>(
        floor,
        Transform{.rotation = glm::angleAxis(-glm::half_pi<float>(),
                                             glm::vec3(1.0f, 0.0f, 0.0f)),
                  .scale = {1, 1, 1}});
    reg.emplace<PendingMesh>(floor, createQuad(floorSize, floorSize),
                             glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));
    auto ground = reg.create();
    reg.emplace<Transform>(
        ground, Transform{.position = {0, -0.5f, 0}, .scale = {1, 1, 1}});
    reg.emplace<JPH::BodyCreationSettings>(
        ground, new JPH::BoxShape(JPH::Vec3(floorSize / 2, 0.5f, floorSize / 2)),
        JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static,
        NON_MOVING);

    auto player = reg.create();
    reg.emplace<Player>(player);
    reg.emplace<Transform>(
        player, Transform{.position = {0, 3, 0}, .scale = {1, 1, 1}});
    reg.emplace<PendingMesh>(player, createCapsule(halfHeight, radius),
                             glm::vec4(0.2f, 0.8f, 0.2f, 1.0f));
    auto &body = reg.emplace<JPH::BodyCreationSettings>(
        player, new JPH::CapsuleShape(halfHeight, radius), JPH::RVec3::sZero(),
        JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, MOVING);
    body.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                        JPH::EAllowedDOFs::TranslationY |
                        JPH::EAllowedDOFs::TranslationZ; // never tips over
    body.mFriction = 0.0f; // velocity is set directly, friction only drags walls

    auto camera = reg.create();
    reg.emplace<ThirdPersonCamera>(camera, ThirdPersonCamera{.target = player});
  }

  // The shader multiplies material color by the texture, so the color needs
  // a white one under it (getTexture(reg, "") is the fuchsia fallback).
  std::shared_ptr<Texture> white;

  void start(entt::registry &reg) override {
    auto &render = renderer(reg);
    if (!white) {
      constexpr unsigned char pixel[4] = {255, 255, 255, 255};
      white = loadTextureFromPixels(pixel, 1, 1);
    }
    for (entt::entity e : ModelPlugin::collect(reg.view<PendingMesh>())) {
      const PendingMesh pending = reg.get<PendingMesh>(e);
      render.spawn(reg, e, pending.mesh, white, {});
      Material &m = *reg.get<MaterialRef>(e).material;
      m.color = pending.color;
      m.metallic = 0.0f; // the default 1 renders near black without reflections
      reg.remove<PendingMesh>(e);
    }
  }

  // Runs before PhysicsPlugin::update, so the velocity set here is stepped
  // this frame.
  void update(entt::registry &reg) override {
    const Core *core = Core::get();
    const EventManager &input = *core->eventManager;
    const auto &frame = reg.ctx().get<FrameContext>();
    auto &bodies = core->physicsManager->bodies();

    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    for (auto [e, cam] : reg.view<ThirdPersonCamera>().each()) {
      forward = cam.front();
    }
    forward.y = 0.0f;
    forward = glm::normalize(forward);
    const glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));

    for (auto [e, player, t, id] :
         reg.view<Player, Transform, JPH::BodyID>().each()) {
      glm::vec3 dir{0.0f};
      if (!core->frozen && !frame.uiCapturesKeyboard) {
        if (input.down(SDL_SCANCODE_W)) {
          dir += forward;
        }
        if (input.down(SDL_SCANCODE_S)) {
          dir -= forward;
        }
        if (input.down(SDL_SCANCODE_D)) {
          dir += right;
        }
        if (input.down(SDL_SCANCODE_A)) {
          dir -= right;
        }
      }
      if (glm::dot(dir, dir) > 0.0f) {
        dir = glm::normalize(dir) * player.speed;
      }
      JPH::Vec3 v = bodies.GetLinearVelocity(id);
      v.SetX(dir.x);
      v.SetZ(dir.z);
      // Just under the capsule's bottom, so the ray can't hit the capsule.
      const glm::vec3 feet =
          glm::vec3(t.position) - glm::vec3(0.0f, halfHeight + radius + 0.01f, 0.0f);
      const bool grounded = castRay(feet, glm::vec3(0.0f, -0.1f, 0.0f));
      if (grounded && !core->frozen && !frame.uiCapturesKeyboard &&
          input.pressed(SDL_SCANCODE_SPACE)) {
        v.SetY(player.jumpSpeed);
      }
      bodies.SetLinearVelocity(id, v); // wakes the body when non-zero
    }
  }
};
