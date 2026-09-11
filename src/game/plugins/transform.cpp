#include "transform.hpp"
#include "glm/ext/matrix_transform.hpp"

using namespace KR;
glm::mat4 Transform::matrix() const {
  return glm::translate(glm::mat4(1.0f), position) *
         glm::mat4_cast(rotation) *
         glm::scale(glm::mat4(1.0f), scale);
}
