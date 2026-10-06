#pragma once
#include "../render/render.hpp"
#include "../transform/transform.hpp"
#include "physics/raycast.hpp"
#include <Kuru.h>
#include <limits>
using namespace KR;

// Tag: left-clicking this entity's model selects it.
struct Pickable {};

// Ray vs axis-aligned box (slab test). The ray is origin + t * dir; returns
// the entry t (0 if the origin is inside), or a negative value on a miss.
inline float rayBox(glm::vec3 origin, glm::vec3 dir, glm::vec3 lo,
                    glm::vec3 hi) {
  float tNear = 0.0f, tFar = std::numeric_limits<float>::max();
  for (int a = 0; a < 3; a++) {
    if (std::abs(dir[a]) < 1e-12f) {
      // Parallel to this slab: inside it or never.
      if (origin[a] < lo[a] || origin[a] > hi[a])
        return -1.0f;
      continue;
    }
    float t0 = (lo[a] - origin[a]) / dir[a];
    float t1 = (hi[a] - origin[a]) / dir[a];
    if (t0 > t1)
      std::swap(t0, t1);
    tNear = std::max(tNear, t0);
    tFar = std::min(tFar, t1);
    if (tNear > tFar)
      return -1.0f;
  }
  return tNear;
}

struct PickingPlugin : public Plugin {
  PickingPlugin() { registerComponent<Pickable>(); }

  // Registered after TransformPlugin: a click on the gizmo has already set
  // uiCapturesMouse by now, and so has one on any ImGui window.
  void update(entt::registry &r) override {
    const auto &frame = r.ctx().get<FrameContext>();
    if (frame.uiCapturesMouse)
      return;
    for (const SDL_Event &event : Core::get()->eventManager->events) {
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          event.button.button == SDL_BUTTON_LEFT)
        pick(r, frame, {event.button.x, event.button.y});
    }
  }

  // Selects the nearest Pickable whose model box the cursor's ray hits, or
  // clears the selection on a miss.
  void pick(entt::registry &r, const FrameContext &frame, glm::vec2 pixel) {
    const glm::vec3 origin = glm::inverse(frame.view)[3];
    // To the far plane: t runs 0..1 along it.
    const glm::vec3 dir = screenToWorld(frame, pixel) - origin;
    entt::entity best = entt::null;
    float bestT = std::numeric_limits<float>::max();
    for (auto [e, meshRef, t] :
         r.view<Pickable, MeshRef, Transform>().each()) {
      // Into model space, so the box turns and scales with the entity. Not
      // renormalized: t stays the same parameter along the world ray, so hits
      // on different entities compare directly.
      const glm::mat4 inv = glm::inverse(t.matrix());
      const float hit = rayBox(glm::vec3(inv * glm::vec4(origin, 1.0f)),
                               glm::vec3(inv * glm::vec4(dir, 0.0f)),
                               meshRef.mesh->boundsMin, meshRef.mesh->boundsMax);
      if (hit >= 0.0f && hit < bestT) {
        bestT = hit;
        best = e;
      }
    }
    // Ctrl: toggle the hit into the selection, keep it on a miss.
    if (!ImGui::GetIO().KeyCtrl) {
      r.clear<Selected>();
    }
    if (best != entt::null && r.remove<Selected>(best) == 0) {
      r.emplace<Selected>(best);
    }
  }
};
