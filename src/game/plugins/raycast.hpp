#pragma  once
#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include <Kuru.h>
#include "plugin/plugin.hpp"

using namespace KR;
inline bool castRay(glm::vec3 from, glm::vec3 to) {
  using namespace JPH;
  const RRayCast ray{RVec3(glmVecToJPH(from)), glmVecToJPH(to)};
  RayCastResult hit;
  return Core::get()->physicsManager->system.GetNarrowPhaseQuery().CastRay(ray,
                                                                           hit);
}

inline glm::vec3 screenToWorld(const FrameContext &frame, glm::vec2 pixel) {
  const glm::vec4 ndc{2.0f * pixel.x / frame.extent.width - 1.0f,
                      2.0f * pixel.y / frame.extent.height - 1.0f, 1.0f, 1.0f};
  const glm::vec4 world = glm::inverse(frame.skyRayProj * frame.view) * ndc;
  return glm::vec3(world) / world.w;
}

inline bool worldToScreen(const FrameContext &frame, glm::vec3 world,
                          glm::vec2 &pixel) {
  const glm::vec4 clip = frame.skyRayProj * frame.view * glm::vec4(world, 1.0f);
  if (clip.w <= 0.0f) return false;
  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  pixel = {(ndc.x + 1.0f) * 0.5f * frame.extent.width,
           (ndc.y + 1.0f) * 0.5f * frame.extent.height};
  return true;
}
