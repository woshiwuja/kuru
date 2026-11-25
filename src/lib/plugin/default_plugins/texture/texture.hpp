#pragma once
#include "../render/render.hpp"
#include "common/common.hpp"
#include "entt/entity/fwd.hpp"
#include "imgui.h"
#include <Kuru.h>
#include <filesystem>
#include <string>

using namespace KR;

struct TexturePath {
    std::string val;
};

struct TextureLoader {
  using result_type = std::shared_ptr<Texture>;
  result_type operator()(const std::string &path) const {
    return loadTexture(path);
  }
};

using TextureCache = entt::resource_cache<Texture, TextureLoader>;
std::shared_ptr<Texture> getTexture(entt::registry &reg,
                                    const std::string &path);

struct TexturePlugin : public Plugin {
    TexturePlugin(){
        registerComponent<TexturePath>();
        registerComponent<Texture>();
    }
    void UI(entt::registry &r);
    void update(entt::registry &r) override;
};
