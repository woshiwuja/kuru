#include "transform.hpp"

using namespace KR;
glm::mat4 Transform::matrix() const {
  return glm::translate(glm::mat4(1.0f), glm::vec3(position)) *
         glm::mat4_cast(rotation) *
         glm::scale(glm::mat4(1.0f), scale);
}
