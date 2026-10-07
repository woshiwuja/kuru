#include "model.hpp"
#include "../common/common.hpp"
#include "../core/core.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <limits>
// tiny_gltf.h is the only place stb_image.h comes from, and its implementation
// block sits outside the include guard: keep it to this one TU.
// We only ever read glTF, so drop the writer rather than link stb_image_write.
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

namespace KR {

void Mesh::upload(const std::vector<Vertex> &vertices,
                  const std::vector<uint32_t> &indices) {
  assert(!vertices.empty() && !indices.empty());
  auto &device = Core::get()->device;
  indexCount = static_cast<uint32_t>(indices.size());

  glm::vec3 lo(std::numeric_limits<float>::max()), hi(-lo);
  for (const Vertex &v : vertices) {
    lo = glm::min(lo, v.pos);
    hi = glm::max(hi, v.pos);
  }
  boundsMin = lo;
  boundsMax = hi;
  boundsCenter = (lo + hi) * 0.5f;
  boundsRadius = 0.0f;
  for (const Vertex &v : vertices)
    boundsRadius = std::max(boundsRadius, glm::distance(v.pos, boundsCenter));

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

namespace {

glm::mat4 nodeLocalMatrix(const tinygltf::Node &node) {
  if (node.matrix.size() == 16) {
    glm::mat4 m;
    for (int c = 0; c < 4; c++) {
      for (int r = 0; r < 4; r++) {
        m[c][r] = static_cast<float>(node.matrix[c * 4 + r]);
      }
    }
    return m;
  }
  glm::mat4 m(1.0f);
  if (node.translation.size() == 3) {
    m = glm::translate(m, glm::vec3(node.translation[0], node.translation[1],
                                    node.translation[2]));
  }
  if (node.rotation.size() == 4) {
    m *= glm::mat4_cast(glm::quat(
        static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
        static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));
  }
  if (node.scale.size() == 3) {
    m = glm::scale(m, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
  }
  return m;
}

// One drawn instance of a glTF mesh: the node's world matrix bakes into the
// vertices.
struct MeshInstance {
  int mesh = 0;
  glm::mat4 matrix{1.0f};
  std::string name;
};

bool readGltf(const std::string &path, tinygltf::Model &model) {
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
    std::cout << "glTF: could not load \"" << path
              << "\", using a placeholder cube instead\n";
  }
  return ret;
}

// Every mesh node of the default scene, in scene order.
std::vector<MeshInstance> meshInstances(const tinygltf::Model &model) {
  std::vector<MeshInstance> instances;
  const auto visit = [&](auto &&self, int nodeIndex,
                         const glm::mat4 &parent) -> void {
    const tinygltf::Node &node = model.nodes[nodeIndex];
    const glm::mat4 world = parent * nodeLocalMatrix(node);
    if (node.mesh >= 0) {
      instances.push_back({node.mesh, world, node.name});
    }
    for (int child : node.children) {
      self(self, child, world);
    }
  };
  if (!model.scenes.empty()) {
    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    for (int root : model.scenes[sceneIndex].nodes) {
      visit(visit, root, glm::mat4(1.0f));
    }
  } else {
    for (size_t i = 0; i < model.meshes.size(); i++) {
      instances.push_back({static_cast<int>(i), glm::mat4(1.0f),
                           model.meshes[i].name});
    }
  }
  return instances;
}

// Base-colour texture per material slot (see materialSlot in buildMesh),
// loaded on first use: the parts of one model share them.
struct MaterialTextures {
  const tinygltf::Model &model;
  std::vector<std::shared_ptr<Texture>> slots;

  explicit MaterialTextures(const tinygltf::Model &m)
      : model(m), slots(m.materials.size() + 1) {}

  std::shared_ptr<Texture> get(size_t slot) {
    if (!slots[slot]) {
      slots[slot] = load(static_cast<int>(slot) - 1);
    }
    return slots[slot];
  }

  std::shared_ptr<Texture> load(int materialIndex) const {
    std::vector<double> factor = {1.0, 1.0, 1.0, 1.0};
    if (materialIndex >= 0 &&
        materialIndex < static_cast<int>(model.materials.size())) {
      const auto &pbr = model.materials[materialIndex].pbrMetallicRoughness;
      factor = pbr.baseColorFactor;
      const int texIndex = pbr.baseColorTexture.index;
      const int imgIndex =
          texIndex >= 0 && texIndex < static_cast<int>(model.textures.size())
              ? model.textures[texIndex].source
              : -1;
      if (imgIndex >= 0 && imgIndex < static_cast<int>(model.images.size()) &&
          !model.images[imgIndex].image.empty()) {
        const tinygltf::Image &image = model.images[imgIndex];
        return loadTextureFromPixels(image.image.data(),
                                     static_cast<uint32_t>(image.width),
                                     static_cast<uint32_t>(image.height));
      }
    }
    // The factor is linear, the texture is sampled as sRGB: encode it.
    const auto toSrgb8 = [](double c) {
      c = std::clamp(c, 0.0, 1.0);
      c = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
      return static_cast<unsigned char>(std::lround(c * 255.0));
    };
    const unsigned char pixel[4] = {toSrgb8(factor[0]), toSrgb8(factor[1]),
                                    toSrgb8(factor[2]),
                                    static_cast<unsigned char>(std::lround(
                                        std::clamp(factor[3], 0.0, 1.0) * 255.0))};
    return loadTextureFromPixels(pixel, 1, 1);
  }
};

// One Mesh from `instances`, vertices in model space.
std::shared_ptr<Mesh> buildMesh(const tinygltf::Model &model,
                                const std::vector<MeshInstance> &instances,
                                MaterialTextures &textures) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<SubMesh> submeshes;
  // World height range, used by MapPlugin to build the physics heightfield.
  float minY = std::numeric_limits<float>::max();
  float maxY = std::numeric_limits<float>::lowest();

  // Indices are grouped by material and become one submesh (one texture, one
  // descriptor set per frame) each. Per primitive would be per node instance
  // too, since nodes reuse meshes: thousands of texture uploads and sets.
  // Slot 0 is "no/invalid material", slot i + 1 is material i.
  const auto materialSlot = [&](int materialIndex) -> size_t {
    if (materialIndex >= 0 &&
        materialIndex < static_cast<int>(model.materials.size())) {
      return static_cast<size_t>(materialIndex) + 1;
    }
    return 0;
  };
  std::vector<std::vector<uint32_t>> slotIndices(model.materials.size() + 1);

  for (const MeshInstance &instance : instances) {
    const glm::mat4 &nodeMatrix = instance.matrix;
    const tinygltf::Mesh &mesh = model.meshes[instance.mesh];
    const glm::mat3 normalMatrix = glm::mat3(nodeMatrix);
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
        vertex.pos = glm::vec3(nodeMatrix * glm::vec4(pos[0], pos[1], pos[2], 1.0f));
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
          vertex.normal = glm::normalize(
              normalMatrix * glm::vec3(normal[0], normal[1], normal[2]));
        } else {
          vertex.normal = {0.0f, 1.0f, 0.0f};
        }

        vertex.color = {1.0f, 1.0f, 1.0f};

        vertices.push_back(vertex);
      }

      std::vector<uint32_t> &slotIdx = slotIndices[materialSlot(primitive.material)];
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

      slotIdx.reserve(slotIdx.size() + indexCount);

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

        slotIdx.push_back(baseVertex + index);
      }
    }
  }

  for (size_t slot = 0; slot < slotIndices.size(); slot++) {
    if (slotIndices[slot].empty()) {
      continue;
    }
    const uint32_t indexOffset = static_cast<uint32_t>(indices.size());
    indices.insert(indices.end(), slotIndices[slot].begin(),
                   slotIndices[slot].end());
    submeshes.push_back(SubMesh{indexOffset,
                                static_cast<uint32_t>(slotIndices[slot].size()),
                                textures.get(slot)});
  }

  auto mesh = std::make_shared<Mesh>();
  mesh->minY = minY;
  mesh->maxY = maxY;
  mesh->vertices = vertices;
  mesh->indices = indices;
  mesh->upload(vertices, indices);
  mesh->submeshes = std::move(submeshes);

  return mesh;
}

} // namespace

std::shared_ptr<Mesh> loadModel(const std::string &path) {
  tinygltf::Model model;
  if (!readGltf(path, model)) {
    return createCube();
  }
  MaterialTextures textures(model);
  return buildMesh(model, meshInstances(model), textures);
}

std::vector<ModelPart> loadModelParts(const std::string &path) {
  tinygltf::Model model;
  if (!readGltf(path, model)) {
    return {{path, createCube()}};
  }
  MaterialTextures textures(model);
  std::vector<ModelPart> parts;
  for (const MeshInstance &instance : meshInstances(model)) {
    parts.push_back({instance.name, buildMesh(model, {instance}, textures)});
  }
  if (parts.empty()) {
    parts.push_back({path, createCube()});
  }
  return parts;
}

std::shared_ptr<Mesh> createSphere(float radius, uint32_t rings,
                                   uint32_t sectors) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  const uint32_t columns = sectors + 1;
  for (uint32_t r = 0; r <= rings; r++) {
    const float v = static_cast<float>(r) / static_cast<float>(rings);
    const float phi = v * glm::pi<float>();
    const float y = std::cos(phi);
    const float ringRadius = std::sin(phi);

    for (uint32_t s = 0; s <= sectors; s++) {
      const float u = static_cast<float>(s) / static_cast<float>(sectors);
      const float theta = u * glm::two_pi<float>();
      const glm::vec3 normal(ringRadius * std::cos(theta), y,
                             ringRadius * std::sin(theta));

      Vertex vertex{};
      vertex.pos = normal * radius;
      vertex.normal = normal;
      vertex.texCoord = {u, v};
      vertex.color = {1.0f, 1.0f, 1.0f};
      vertices.push_back(vertex);
    }
  }

  for (uint32_t r = 0; r < rings; r++) {
    for (uint32_t s = 0; s < sectors; s++) {
      const uint32_t i0 = r * columns + s;
      const uint32_t i1 = i0 + columns;
      indices.push_back(i0);
      indices.push_back(i1);
      indices.push_back(i0 + 1);
      indices.push_back(i0 + 1);
      indices.push_back(i1);
      indices.push_back(i1 + 1);
    }
  }

  auto mesh = std::make_shared<Mesh>();
  mesh->minY = -radius;
  mesh->maxY = radius;
  mesh->vertices = vertices;
  mesh->indices = indices;
  mesh->upload(vertices, indices);
  return mesh;
}

std::shared_ptr<Mesh> createCube(float halfExtent) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  struct Face {
    glm::vec3 normal, right, up;
  };
  const Face faces[6] = {
      {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   // +Z
      {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, // -Z
      {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  // +X
      {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  // -X
      {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  // +Y
      {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},  // -Y
  };
  const glm::vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

  for (const Face &face : faces) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const glm::vec3 center = face.normal * halfExtent;
    const glm::vec3 corners[4] = {
        center - face.right * halfExtent - face.up * halfExtent,
        center + face.right * halfExtent - face.up * halfExtent,
        center + face.right * halfExtent + face.up * halfExtent,
        center - face.right * halfExtent + face.up * halfExtent,
    };
    for (int i = 0; i < 4; i++) {
      Vertex vertex{};
      vertex.pos = corners[i];
      vertex.normal = face.normal;
      vertex.texCoord = uvs[i];
      vertex.color = {1.0f, 1.0f, 1.0f};
      vertices.push_back(vertex);
    }
    // right x up == normal for every face above, so this winding is CCW as
    // seen from outside on all six - matching the pipeline's front face.
    for (uint32_t i : {0u, 1u, 2u, 2u, 3u, 0u}) {
      indices.push_back(base + i);
    }
  }

  auto mesh = std::make_shared<Mesh>();
  mesh->minY = -halfExtent;
  mesh->maxY = halfExtent;
  mesh->vertices = vertices;
  mesh->indices = indices;
  mesh->upload(vertices, indices);
  return mesh;
}
std::shared_ptr<Mesh> createCone(float radius, float height,
                                 uint32_t sectors) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // One flat-shaded triangle at a time, wound like createCube: (b - a) x
  // (c - a) points out of the cone.
  const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    for (const glm::vec3 &p : {a, b, c}) {
      Vertex vertex{};
      vertex.pos = p;
      vertex.normal = normal;
      vertex.color = {1.0f, 1.0f, 1.0f};
      indices.push_back(static_cast<uint32_t>(vertices.size()));
      vertices.push_back(vertex);
    }
  };
  const auto rim = [&](uint32_t s) {
    const float a = glm::two_pi<float>() * static_cast<float>(s) /
                    static_cast<float>(sectors);
    return glm::vec3(radius * std::cos(a), height, radius * std::sin(a));
  };
  const glm::vec3 tip(0.0f), capCenter(0.0f, height, 0.0f);
  for (uint32_t s = 0; s < sectors; s++) {
    triangle(tip, rim(s), rim(s + 1));
    triangle(capCenter, rim(s + 1), rim(s));
  }

  auto mesh = std::make_shared<Mesh>();
  mesh->minY = 0.0f;
  mesh->maxY = height;
  mesh->vertices = vertices;
  mesh->indices = indices;
  mesh->upload(vertices, indices);
  return mesh;
}
} // namespace KR
