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

  bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, assetPath(path));

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
  std::vector<SubMesh> submeshes;
  // World height range, fed to the terrain shading in the UBO.
  float minY = std::numeric_limits<float>::max();
  float maxY = std::numeric_limits<float>::lowest();

  // One texture per primitive's own material, if it has one. tinygltf decodes
  // embedded/external images to tightly-packed RGBA8 by default.
  const auto loadMaterialTexture =
      [&](int materialIndex) -> std::shared_ptr<Texture> {
    if (materialIndex < 0 ||
        materialIndex >= static_cast<int>(model.materials.size())) {
      return nullptr;
    }
    const auto &material = model.materials[materialIndex];
    const int texIndex = material.pbrMetallicRoughness.baseColorTexture.index;
    if (texIndex < 0 || texIndex >= static_cast<int>(model.textures.size())) {
      return nullptr;
    }
    const int imgIndex = model.textures[texIndex].source;
    if (imgIndex < 0 || imgIndex >= static_cast<int>(model.images.size())) {
      return nullptr;
    }
    const tinygltf::Image &image = model.images[imgIndex];
    if (image.image.empty()) {
      return nullptr;
    }
    return loadTextureFromPixels(image.image.data(),
                                 static_cast<uint32_t>(image.width),
                                 static_cast<uint32_t>(image.height));
  };

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

      // Get normals if available; meshes without them fall back to a flat
      // up-facing normal rather than zero, which would light as pure black.
      bool hasNormals =
          primitive.attributes.find("NORMAL") != primitive.attributes.end();
      const tinygltf::Accessor *normalAccessor = nullptr;
      const tinygltf::BufferView *normalBufferView = nullptr;
      const tinygltf::Buffer *normalBuffer = nullptr;

      if (hasNormals) {
        normalAccessor = &model.accessors[primitive.attributes.at("NORMAL")];
        normalBufferView = &model.bufferViews[normalAccessor->bufferView];
        normalBuffer = &model.buffers[normalBufferView->buffer];
      }

      uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

      for (size_t i = 0; i < posAccessor.count; i++) {
        Vertex vertex{};

        const float *pos = reinterpret_cast<const float *>(
            &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset +
                            i * 12]);
        vertex.pos = {pos[0], pos[1], pos[2]};
        minY = std::min(minY, vertex.pos.y);
        maxY = std::max(maxY, vertex.pos.y);

        if (hasTexCoords) {
          const float *texCoord = reinterpret_cast<const float *>(
              &texCoordBuffer->data[texCoordBufferView->byteOffset +
                                    texCoordAccessor->byteOffset + i * 8]);
          vertex.texCoord = {texCoord[0], texCoord[1]};
        } else {
          vertex.texCoord = {0.0f, 0.0f};
        }

        if (hasNormals) {
          const float *normal = reinterpret_cast<const float *>(
              &normalBuffer->data[normalBufferView->byteOffset +
                                  normalAccessor->byteOffset + i * 12]);
          vertex.normal = {normal[0], normal[1], normal[2]};
        } else {
          vertex.normal = {0.0f, 1.0f, 0.0f};
        }

        vertex.color = {1.0f, 1.0f, 1.0f};

        vertices.push_back(vertex);
      }

      const uint32_t indexOffset = static_cast<uint32_t>(indices.size());
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

      submeshes.push_back(SubMesh{indexOffset, static_cast<uint32_t>(indexCount),
                                  loadMaterialTexture(primitive.material)});
    }
  }

  auto mesh = std::make_shared<Mesh>();
  mesh->minY = minY;
  mesh->maxY = maxY;
  mesh->upload(vertices, indices);
  mesh->submeshes = std::move(submeshes);

  return mesh;
}
