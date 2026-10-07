#pragma once
#include "../render/render.hpp"
#include "common/common.hpp"
#include "entt/entity/fwd.hpp"
#include "imgui.h"
#include <Kuru.h>
#include <filesystem>
#include <string>

using namespace KR;

// Texture for the entity's ModelPath spawn, or a swap on an already spawned
// one; ModelPlugin::start consumes it. getTexture() lives in render.hpp.
struct TexturePath {
    std::string val;
};
struct TexturePlugin : public Plugin {
    TexturePlugin(){
        registerComponent<TexturePath>();
        registerComponent<Texture>();
    }
    void update(entt::registry &r) override {
            UI(r);
    }
    void UI(entt::registry &r){
        using namespace ImGui;
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
                namespace fs = std::filesystem;
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
};
