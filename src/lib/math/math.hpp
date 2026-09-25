#pragma once
#include "glm/glm.hpp"
#include <imgui.h>
#include "Jolt/Jolt.h"
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
  vec3& operator +=(glm::vec3 v){
      x+=v.x;
      y+=v.y;
      z+=v.z;
      return *this;
  }
  vec3 operator +(glm::vec3 v){
      auto new_v = *this;
      new_v += v;
      return new_v;
  };
  void normalize();
};

struct quat {
  double x, y, z, w = 0;
  quat(float radians) {}
};
} // namespace KR
