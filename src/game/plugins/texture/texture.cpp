#include "texture.hpp"
#include "entt/entity/fwd.hpp"
using TextureCache = entt::resource_cache<Texture, TextureLoader>;
std::shared_ptr<Texture> getTexture(entt::registry &reg,
                                    const std::string &path) {
  auto *cache = reg.ctx().find<TextureCache>();
  if (cache == nullptr) {
    cache = &reg.ctx().emplace<TextureCache>();
  }
  return cache->load(entt::hashed_string::value(path.c_str()), path)
      .first->second.handle();
}

void TexturePlugin::UI(entt::registry &r){
    using namespace ImGui;
    namespace fs = std::filesystem;
    Begin("Texture");
    for (entt::entity e : r.view<Selected>()) {
        PushID(static_cast<int>(entt::to_integral(e)));
        const auto *model = r.try_get<Model>(e);
        const char *current = "(none)";
        if (const auto *pending = r.try_get<TexturePath>(e)) {
            current = pending->val.c_str();
        } else if (model && model->texture[0]) {
            current = model->texture;
        }
        if (BeginCombo("texture", current)) {
            for (const auto &entry :
                fs::directory_iterator(assetPath("textures"))) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const std::string path =
                    "textures/" + entry.path().filename().string();
                if (Selectable(path.c_str(), path == current)) {
                    r.emplace_or_replace<TexturePath>(e, path);
                }
            }
            EndCombo();
        }
        PopID();
    }
    End();
}

void TexturePlugin::update(entt::registry &r){
            UI(r);
}
