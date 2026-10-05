#pragma once
#include "../render/render.hpp"
#include "../material/material.hpp"
#include "imgui.h"
#include <Kuru.h>
#include <filesystem>
using namespace KR;

// One-shot request: ModelPlugin::start loads `path` into the entity's
// MeshRef, then removes this.
struct ModelPath {
  std::string path;
};

struct ModelPlugin : public Plugin {
  ModelPlugin() { registerComponent<ModelPath>(); }

  // Mesh swaps go here, not in update(): this frame's commands don't
  // reference the old mesh yet, and the renderer's `retired` list covers the
  // frames in flight. Runs after RenderPlugin::start (registration order).
  void start(entt::registry &r) override {
    auto &render = renderer(r);
    auto requests = r.view<MeshRef, ModelPath>();
    const std::vector<entt::entity> swaps(requests.begin(), requests.end());
    for (entt::entity e : swaps) {
      const std::string path = r.get<ModelPath>(e).path;
      auto &meshRef = r.get<MeshRef>(e);
      RenderPlugin::Retired old{.frame = render.frameCount};
      old.mesh = std::exchange(meshRef.mesh, loadModel(path));
      if (r.all_of<Prop>(e)) {
        // Props draw through a batch per mesh, so the new mesh needs one.
        auto texture =
            render.propBatches.at(old.mesh.get()).material->baseColor;
        auto [it, fresh] = render.propBatches.try_emplace(meshRef.mesh.get());
        if (fresh) {
          it->second.mesh = meshRef.mesh;
          it->second.material =
              std::make_shared<Material>(Material{.baseColor = texture});
          it->second.renderable = render.makeRenderable(*meshRef.mesh, texture);
        }
      } else if (auto *material = r.try_get<MaterialRef>(e)) {
        // Descriptor sets are per submesh: the new mesh needs its own.
        if (auto *old_r = r.try_get<Renderable>(e))
          old.renderable = std::move(*old_r);
        r.emplace_or_replace<Renderable>(
            e, render.makeRenderable(*meshRef.mesh,
                                     material->material->baseColor));
      }
      // Keep saves pointing at the new file.
      if (auto *model = r.try_get<Model>(e)) {
        assert(path.size() < sizeof(model->mesh));
        snprintf(model->mesh, sizeof(model->mesh), "%s", path.c_str());
      }
      render.retired.push_back(std::move(old));
      r.remove<ModelPath>(e);
    }
  }

  void update(entt::registry &r) override { UI(r); }

  // Swap the selected entity's mesh: picking a file queues a ModelPath,
  // which start() applies next frame.
  void UI(entt::registry &r) {
    if (ImGui::Begin("Model")) {
      for (auto [e, meshRef] : r.view<Selected, MeshRef>().each()) {
        ImGui::PushID(static_cast<int>(entt::to_integral(e)));
        const auto *model = r.try_get<Model>(e);
        if (ImGui::BeginCombo("mesh", model ? model->mesh : "(in-memory)")) {
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
    }
    ImGui::End();
  }
};
