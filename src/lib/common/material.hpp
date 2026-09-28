#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "../image/image.hpp"

namespace KR{
struct Material {
  glm::vec4 color{1.0f};
  float metallic = 1.0f, roughness = 1.0f; 
  float alphaCutoff = 0.5f;
  bool alphaMask = false;
  std::shared_ptr<Texture> baseColor, metallicRoughness, normal, occlusion,
      emissive;
  vk::raii::DescriptorSet descriptorSet =
      nullptr;
};
}