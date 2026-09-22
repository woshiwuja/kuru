#pragma once
#include "glm/glm.hpp"
#include <imgui.h>
#include "Jolt/Jolt.h"
#include "Jolt/Math/Real.h"
namespace KR {
struct vec2 {
  double x = 0;
  double y = 0;
  operator glm::vec2() const { return glm::vec2(x, y); }
  operator ImVec2() const { return ImVec2(x, y); }
};

struct vec3 {
  double x = 0;
  double y = 0;
  double z = 0;
  operator glm::vec3() const { return glm::vec3(x, y, z); }
  operator JPH::RVec3() const {
    return JPH::RVec3(double(x), double(y), double(z));
  }
};

inline float lenght(vec2 v) { return sqrt(v.x * v.x + v.y * v.y); }
inline float lenght(vec3 v) { return sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline vec2 normalize(vec2 v) {
  auto l = lenght(v);
  if (l < 1e-12f)
    return vec2{0.f, 0.f};
  vec2 n = vec2(v.x / l, v.y / l);
  return n;
}
inline vec3 normalize(vec3 v) {
  auto l = lenght(v);
  if (l < 1e-12f)
    return vec3{0.f, 0.f, 0.f};
  vec3 n = vec3(v.x / l, v.y / l, v.z / l);
  return n;
}
struct quat {
  double x, y, z, w = 0;
  quat(float radians) {}
};
} // namespace KR
