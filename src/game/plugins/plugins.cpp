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

entt::entity spawn(entt::registry &reg, const std::string &meshPath,
                   const std::string &texturePath, glm::vec4 params,
                   Transform transform) {
  return renderer(reg).spawn(reg, getMesh(reg, meshPath).handle(),
                             getTexture(reg, texturePath), params, transform);
}
