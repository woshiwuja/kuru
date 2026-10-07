#pragma once
#include "../render/render.hpp"
#include "../material/material.hpp"
#include "../texture/texture.hpp"
#include "imgui.h"
#include <Kuru.h>
#include <filesystem>
using namespace KR;

struct ModelPath {
  std::string path;
};

struct ModelPlugin : public Plugin {
  ModelPlugin() { registerComponent<ModelPath>(); }
  void start(entt::registry &r) override {
    auto &render = renderer(r);
    // Not spawned yet: spawn it from its paths, as a prop if it has Prop.
    for (entt::entity e : collect(r.view<ModelPath>(entt::exclude<MeshRef>))) {
      const std::string path = r.get<ModelPath>(e).path;
      if (path.empty()) {
        continue;
      }
      std::string texture;
      if (const auto *t = r.try_get<TexturePath>(e)) {
        texture = t->val;
      }
      if (r.all_of<Prop>(e)) {
        spawnProp(r, e, path, texture);
      } else {
        spawn(r, e, path, texture);
      }
      r.remove<ModelPath, TexturePath>(e);
    }

    // ModelPath on an existing mesh: swap it.
    for (entt::entity e : collect(r.view<MeshRef, ModelPath>())) {
      const std::string path = r.get<ModelPath>(e).path;
      if (path.empty())
        continue;
      // The old model's nodes go, the new one's come below.
      render.despawnNodes(r, e);
      auto &meshRef = r.get<MeshRef>(e);
      RenderPlugin::Retired old{.frame = render.frameCount};
      old.mesh = std::exchange(meshRef.mesh, render.rootMesh(r, e, path));
      if (r.all_of<Prop>(e)) {
        // Props draw through a batch per mesh; the prop pass below makes the
        // new mesh's.
      } else if (auto *old_r = r.try_get<Renderable>(e)) {
        // Descriptor sets are per submesh: the new mesh needs its own. The
        // drawable pass below rebuilds it.
        old.renderable = std::move(*old_r);
        r.remove<Renderable>(e);
      }
      recordPath(r, e, path);
      render.retired.push_back(std::move(old));
      std::shared_ptr<Texture> texture = getTexture(r, "");
      glm::vec4 params{0.0f};
      if (const auto *ref = r.try_get<MaterialRef>(e)) {
        if (ref->material->baseColor) {
          texture = ref->material->baseColor;
        }
        params = ref->params;
      }
      render.spawnNodes(r, e, path, texture, params);
      r.remove<ModelPath>(e);
    }

    // TexturePath on an existing mesh: swap its texture. The texture is bound
    // in the descriptor sets, so the drawable pass below rebuilds them.
    for (entt::entity e : collect(r.view<MeshRef, TexturePath>())) {
      const std::string path = r.get<TexturePath>(e).val;
      r.remove<TexturePath>(e);
      // ponytail: props share one texture per mesh batch, so they keep theirs;
      // per-instance textures would need a batch per (mesh, texture).
      if (path.empty() || r.all_of<Prop>(e)) {
        continue;
      }
      r.get_or_emplace<MaterialRef>(e).material->baseColor = getTexture(r, path);
      if (auto *own = r.try_get<Renderable>(e)) {
        RenderPlugin::Retired old{.frame = render.frameCount};
        old.renderable = std::move(*own);
        render.retired.push_back(std::move(old));
        r.remove<Renderable>(e);
      }
      auto &model = r.get_or_emplace<Model>(e);
      assert(path.size() < sizeof(model.texture));
      snprintf(model.texture, sizeof(model.texture), "%s", path.c_str());
    }

    // Props draw instanced through a batch per mesh: make sure theirs exists
    // (an inspector-added Prop, a swap above), and drop the entity's own
    // Renderable so it isn't drawn twice.
    for (entt::entity e : collect(r.view<Prop, MeshRef>())) {
      const auto &mesh = r.get<MeshRef>(e).mesh;
      if (!render.propBatches.contains(mesh.get())) {
        std::shared_ptr<Texture> texture = getTexture(r, "");
        std::shared_ptr<Material> material;
        if (const auto *ref = r.try_get<MaterialRef>(e)) {
          if (ref->material->baseColor) {
            texture = ref->material->baseColor;
          }
          material = ref->material;
        }
        render.propBatch(mesh, texture, material);
      }
      r.get_or_emplace<Transform>(e);
      if (auto *own = r.try_get<Renderable>(e)) {
        RenderPlugin::Retired old{.frame = render.frameCount};
        old.renderable = std::move(*own);
        render.retired.push_back(std::move(old));
        r.remove<Renderable>(e);
      }
    }

    // A mesh nothing draws yet (inspector-added MeshRef, a load above, a
    // swap, a removed Prop): give it what drawMeshes needs.
    for (entt::entity e :
         collect(r.view<MeshRef>(entt::exclude<Renderable, Prop>))) {
      r.get_or_emplace<Transform>(e);
      auto &material = r.get_or_emplace<MaterialRef>(e).material;
      // An inspector-added MaterialRef has no texture, and makeRenderable
      // binds one per submesh.
      if (!material->baseColor)
        material->baseColor = getTexture(r, "");
      r.emplace<Renderable>(e, render.makeRenderable(*r.get<MeshRef>(e).mesh,
                                                     material->baseColor));
    }
  }

  template <typename View> static std::vector<entt::entity> collect(View view) {
    return {view.begin(), view.end()};
  }

  static void recordPath(entt::registry &r, entt::entity e,
                         const std::string &path) {
    auto &model = r.get_or_emplace<Model>(e);
    assert(path.size() < sizeof(model.mesh));
    snprintf(model.mesh, sizeof(model.mesh), "%s", path.c_str());
  }

  void update(entt::registry &r) override { UI(r); }

  void UI(entt::registry &r) {
    ImGui::Begin("Model");
      for (entt::entity e : r.view<Selected>()) {
        ImGui::PushID(static_cast<int>(entt::to_integral(e)));
        const auto *model = r.try_get<Model>(e);
        const char *current = "(none)";
        if (model && model->mesh[0]) {
          current = model->mesh;
        } else if (r.all_of<MeshRef>(e)) {
          current = "(in-memory)";
        }
        if (ImGui::BeginCombo("mesh", current)) {
          namespace fs = std::filesystem;
          for (const auto &entry : fs::directory_iterator(assetPath("models"))) {
            if (entry.path().extension() != ".glb")
              continue;
            const std::string path =
                "models/" + entry.path().filename().string();
            if (ImGui::Selectable(path.c_str(), model && path == model->mesh))
              r.emplace_or_replace<ModelPath>(e, path);
          }
          ImGui::EndCombo();
        }
        ImGui::PopID();
      }
    ImGui::End();
  }
};
