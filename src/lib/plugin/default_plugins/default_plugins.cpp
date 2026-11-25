#include "lighting/lighting.hpp"
#include "material/material.hpp"
#include "model/model.hpp"
#include "physics/physics.hpp"
#include "render/mesh_registry.hpp"
#include "render/render.hpp"
#include "texture/texture.hpp"
#include "transform/transform.hpp"
#include "ui/ui.hpp"

std::vector<std::unique_ptr<KR::Plugin>> KR::defaultPlugins() {
  std::vector<std::unique_ptr<Plugin>> plugins;
  plugins.push_back(std::make_unique<PhysicsPlugin>());
  plugins.push_back(std::make_unique<MeshRegistryPlugin>());
  plugins.push_back(std::make_unique<LightingPlugin>());
  plugins.push_back(std::make_unique<RenderPlugin>());
  plugins.push_back(std::make_unique<TransformPlugin>());
  plugins.push_back(std::make_unique<UIPlugin>());
  plugins.push_back(std::make_unique<MaterialPlugin>());
  plugins.push_back(std::make_unique<TexturePlugin>());
  plugins.push_back(std::make_unique<ModelPlugin>());
  return plugins;
}
