#include "model.hpp"
#include "../common/common.hpp"
#include "../core/core.hpp"
#include <iostream>
// tiny_gltf.h is the only place stb_image.h comes from, and its implementation
// block sits outside the include guard: keep it to this one TU.
// We only ever read glTF, so drop the writer rather than link stb_image_write.
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

void Mesh::upload(const std::vector<Vertex> &vertices,
                  const std::vector<uint32_t> &indices) {
  assert(!vertices.empty() && !indices.empty());
  auto &device = Core::get()->device;
  indexCount = static_cast<uint32_t>(indices.size());

  const auto stage = [&](const void *src, vk::DeviceSize size,
                         vk::BufferUsageFlags usage, vk::raii::Buffer &buffer,
                         vk::raii::DeviceMemory &memory) {
    vk::raii::Buffer stagingBuffer = nullptr;
    vk::raii::DeviceMemory stagingBufferMemory = nullptr;
    createBuffer(size, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);
    void *data = stagingBufferMemory.mapMemory(0, size);
    memcpy(data, src, size);
    stagingBufferMemory.unmapMemory();

    createBuffer(size, vk::BufferUsageFlagBits::eTransferDst | usage,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, buffer, memory);
    device->copyBuffer(stagingBuffer, buffer, size);
  };

  stage(vertices.data(), sizeof(vertices[0]) * vertices.size(),
        vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer,
        vertexBufferMemory);
  stage(indices.data(), sizeof(indices[0]) * indices.size(),
        vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer, indexBufferMemory);
}

std::shared_ptr<Mesh> loadModel(const std::string &path) {
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);

  if (!warn.empty()) {
    std::cout << "glTF warning: " << warn << std::endl;
  }

  if (!err.empty()) {
    std::cout << "glTF error: " << err << std::endl;
  }

  if (!ret) {
    throw std::runtime_error("Failed to load glTF model");
  }

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  for (const auto &mesh : model.meshes) {
    for (const auto &primitive : mesh.primitives) {
      // Get indices
      const tinygltf::Accessor &indexAccessor =
          model.accessors[primitive.indices];
      const tinygltf::BufferView &indexBufferView =
          model.bufferViews[indexAccessor.bufferView];
      const tinygltf::Buffer &indexBuffer =
          model.buffers[indexBufferView.buffer];

      // Get vertex positions
      const tinygltf::Accessor &posAccessor =
          model.accessors[primitive.attributes.at("POSITION")];
      const tinygltf::BufferView &posBufferView =
          model.bufferViews[posAccessor.bufferView];
      const tinygltf::Buffer &posBuffer = model.buffers[posBufferView.buffer];

      // Get texture coordinates if available
      bool hasTexCoords =
          primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end();
      const tinygltf::Accessor *texCoordAccessor = nullptr;
      const tinygltf::BufferView *texCoordBufferView = nullptr;
      const tinygltf::Buffer *texCoordBuffer = nullptr;

      if (hasTexCoords) {
        texCoordAccessor =
            &model.accessors[primitive.attributes.at("TEXCOORD_0")];
        texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
        texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
      }

      uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

      for (size_t i = 0; i < posAccessor.count; i++) {
        Vertex vertex{};

        const float *pos = reinterpret_cast<const float *>(
            &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset +
                            i * 12]);
        vertex.pos = {pos[0], pos[1], pos[2]};

        if (hasTexCoords) {
          const float *texCoord = reinterpret_cast<const float *>(
              &texCoordBuffer->data[texCoordBufferView->byteOffset +
                                    texCoordAccessor->byteOffset + i * 8]);
          vertex.texCoord = {texCoord[0], texCoord[1]};
        } else {
          vertex.texCoord = {0.0f, 0.0f};
        }

        vertex.color = {1.0f, 1.0f, 1.0f};

        vertices.push_back(vertex);
      }

      const unsigned char *indexData =
          &indexBuffer
               .data[indexBufferView.byteOffset + indexAccessor.byteOffset];
      size_t indexCount = indexAccessor.count;
      size_t indexStride = 0;

      // Determine index stride based on component type
      if (indexAccessor.componentType ==
          TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        indexStride = sizeof(uint16_t);
      } else if (indexAccessor.componentType ==
                 TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        indexStride = sizeof(uint32_t);
      } else if (indexAccessor.componentType ==
                 TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        indexStride = sizeof(uint8_t);
      } else {
        throw std::runtime_error("Unsupported index component type");
      }

      indices.reserve(indices.size() + indexCount);

      for (size_t i = 0; i < indexCount; i++) {
        uint32_t index = 0;

        if (indexAccessor.componentType ==
            TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
          index =
              *reinterpret_cast<const uint16_t *>(indexData + i * indexStride);
        } else if (indexAccessor.componentType ==
                   TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
          index =
              *reinterpret_cast<const uint32_t *>(indexData + i * indexStride);
        } else if (indexAccessor.componentType ==
                   TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          index =
              *reinterpret_cast<const uint8_t *>(indexData + i * indexStride);
        }

        indices.push_back(baseVertex + index);
      }
    }
  }

  auto mesh = std::make_shared<Mesh>();
  mesh->upload(vertices, indices);
  return mesh;
}

std::shared_ptr<Mesh> loadHeightfield(const std::string &path, float cellSize,
                                      float heightScale) {
  int width = 0, height = 0, channels = 0;
  // Force 1 channel: any greyscale/RGB/RGBA heightmap reads the same way,
  // and stb does the luminance conversion for us.
  stbi_uc *pixels = stbi_load(path.c_str(), &width, &height, &channels, 1);
  if (pixels == nullptr) {
    throw std::runtime_error("failed to load heightmap " + path + ": " +
                             stbi_failure_reason());
  }
  if (width < 2 || height < 2) {
    stbi_image_free(pixels);
    throw std::runtime_error("heightmap must be at least 2x2: " + path);
  }

  auto mesh = std::make_shared<Mesh>();
  mesh->minY = std::numeric_limits<float>::max();
  mesh->maxY = std::numeric_limits<float>::lowest();

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  const float originX = -0.5f * static_cast<float>(width - 1) * cellSize;
  const float originZ = -0.5f * static_cast<float>(height - 1) * cellSize;

  vertices.reserve(static_cast<size_t>(width) * height);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      const float u = static_cast<float>(i) / (width - 1);
      const float v = static_cast<float>(j) / (height - 1);

      const float y =
          static_cast<float>(pixels[static_cast<size_t>(j) * width + i]) /
          255.0f * heightScale;

      Vertex vertex{};
      vertex.pos = {originX + static_cast<float>(i) * cellSize, y,
                    originZ + static_cast<float>(j) * cellSize};
      vertex.color = {1.0f, 1.0f, 1.0f};
      vertex.texCoord = {u, v};
      vertices.push_back(vertex);

      mesh->minY = std::min(mesh->minY, y);
      mesh->maxY = std::max(mesh->maxY, y);
    }
  }
  stbi_image_free(pixels);

  // Two triangles per cell, wound so the +Y face is the front one under
  // eCounterClockwise + eBack culling. Swap C and B below if it renders
  // inside-out.
  indices.reserve(static_cast<size_t>(width - 1) * (height - 1) * 6);
  for (int j = 0; j < height - 1; j++) {
    for (int i = 0; i < width - 1; i++) {
      const uint32_t a = static_cast<uint32_t>(j) * width + i;
      const uint32_t b = a + 1;
      const uint32_t c = a + width;
      const uint32_t d = c + 1;
      for (uint32_t index : {a, c, b, b, c, d}) {
        indices.push_back(index);
      }
    }
  }

  assert(indices.size() == static_cast<size_t>(width - 1) * (height - 1) * 6);
  // The winding above is the easy thing to get backwards, and a culled
  // terrain just looks missing. A ground surface's first triangle must face up.
  const glm::vec3 &p0 = vertices[indices[0]].pos;
  assert(glm::cross(vertices[indices[1]].pos - p0,
                    vertices[indices[2]].pos - p0)
             .y > 0.0f);

  mesh->upload(vertices, indices);
  return mesh;
}
