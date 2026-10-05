#pragma once
#include "../common/vertex.hpp"
#include "../image/image.hpp"
#include <memory>
#include <string>
#include <vulkan/vulkan_raii.hpp>

namespace KR {

struct SubMesh {
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  std::shared_ptr<Texture> texture;
};

struct Mesh {
  vk::raii::Buffer vertexBuffer = nullptr;
  vk::raii::DeviceMemory vertexBufferMemory = nullptr;
  vk::raii::Buffer indexBuffer = nullptr;
  vk::raii::DeviceMemory indexBufferMemory = nullptr;
  uint32_t indexCount = 0;
  float minY = 0.0f;
  float maxY = 0.0f;
  // Model-space bounds, set by upload(): sphere for culling, box for picking.
  glm::vec3 boundsCenter{0.0f};
  float boundsRadius = 0.0f;
  glm::vec3 boundsMin{0.0f};
  glm::vec3 boundsMax{0.0f};
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<SubMesh> submeshes;

  void upload(const std::vector<Vertex> &vertices,
              const std::vector<uint32_t> &indices);
};

std::shared_ptr<Mesh> loadModel(const std::string &path);

std::shared_ptr<Mesh> createSphere(float radius = 0.5f, uint32_t rings = 16,
                                   uint32_t sectors = 32);

std::shared_ptr<Mesh> createCube(float halfExtent = 0.5f);
}
