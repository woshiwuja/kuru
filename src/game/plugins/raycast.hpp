#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "glm/glm.hpp"
#include <Kuru.h>
#include "physics.hpp"
#include "plugin/plugin.hpp"

inline bool castRay(glm::vec3 from, glm::vec3 to) {
  using namespace JPH;
  const RRayCast ray{RVec3(glmVecToJPH(from)), glmVecToJPH(to)};
  RayCastResult hit;
  return Core::get()->physicsManager->system.GetNarrowPhaseQuery().CastRay(ray,
                                                                           hit);
}

inline glm::vec3 screenTarget(const FrameContext &frame, glm::vec2 pixel) {
  const glm::vec4 ndc{2.0f * pixel.x / frame.extent.width - 1.0f,
                      2.0f * pixel.y / frame.extent.height - 1.0f, 1.0f, 1.0f};
  const glm::vec4 world = glm::inverse(frame.skyRayProj * frame.view) * ndc;
  return glm::vec3(world) / world.w;
}
