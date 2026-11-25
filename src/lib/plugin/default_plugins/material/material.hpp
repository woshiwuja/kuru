#pragma once
#include <Kuru.h>
using namespace KR;

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
void materialEditor(Material &m);
struct MaterialRef {
  std::shared_ptr<Material> material = std::make_shared<Material>();
  glm::vec4 params{0.0f};
};
struct MaterialPlugin : public Plugin{
    MaterialPlugin() {
        registerComponent<MaterialRef>();
        registerComponent<Material>();
    }
    void update(entt::registry &r ) override;
    void UI(Material &m);
};
