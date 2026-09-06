#include "plugins.hpp"
#include "mesh_registry.hpp"

RenderPlugin &renderer(entt::registry &reg) {
  auto *plugin = reg.ctx().find<RenderPlugin *>();
  assert(plugin != nullptr && *plugin != nullptr &&
         "RenderPlugin must be registered and initialised first");
  return **plugin;
}

namespace {
struct TextureLoader {
  using result_type = std::shared_ptr<Texture>;
  result_type operator()(const std::string &path) const {
    return loadTexture(path);
  }
};
using TextureCache = entt::resource_cache<Texture, TextureLoader>;
} // namespace

std::shared_ptr<Texture> getTexture(entt::registry &reg,
                                    const std::string &path) {
  auto *cache = reg.ctx().find<TextureCache>();
  if (cache == nullptr) {
    cache = &reg.ctx().emplace<TextureCache>();
  }
  return cache->load(entt::hashed_string::value(path.c_str()), path)
      .first->second.handle();
}

void spawn(entt::registry &reg, entt::entity entity,
           const std::string &meshPath, const std::string &texturePath,
           glm::vec4 params, Transform transform) {
  auto mesh = getMesh(reg, meshPath);
  if (params.x >= 0.5f) {
    params.y = mesh->minY;
    params.z = mesh->maxY;
  }
  // Fallback for any submesh whose glTF primitive had no material of its own
  // (or for a mesh with none at all) - RenderPlugin::attach only reaches for
  // this when a submesh's own texture is null. An empty texturePath still
  // resolves, to Texture::load's fuchsia placeholder.
  std::shared_ptr<Texture> fallbackTexture = getTexture(reg, texturePath);
  renderer(reg).spawn(reg, entity, mesh.handle(), std::move(fallbackTexture),
                      params, transform);
}
