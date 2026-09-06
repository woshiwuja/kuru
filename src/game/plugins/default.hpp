#include "../../lib/plugin/plugin.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "glm/ext/vector_float3.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "plugins.hpp"
#include "render.hpp"
#include <cstring>
#include <format>
#include <string.h>
#include <utility>

struct Character {};
struct Name {
  char *text;
};
struct DefaultPlugin : public Plugin {
  void init(entt::registry &reg) override {
    entt::entity e = reg.create();
    reg.emplace<Character>(e);
    auto s = std::format("Insectman {}", entt::to_integral(e));
    reg.emplace<Name>(e).text = _strdup(s.c_str());
    auto &t = reg.emplace<Transform>(e);
    t.position = glm::vec3{0, 0, 0};
    t.rotation = glm::vec3{0.7, 0.7, 0};
    t.scale = glm::vec3{1, 1, 1};
    spawn(reg, e, "models/insectman.glb", "");

    entt::entity mapEnt = reg.create();
    reg.emplace<Character>(mapEnt);
    auto name = std::format("Testmap {}", entt::to_integral(e));
    reg.emplace<Name>(mapEnt).text = _strdup(name.c_str());
    auto &mapT = reg.emplace<Transform>(mapEnt);
    mapT.position = glm::vec3{0, 0, 0};
    mapT.rotation = glm::vec3{0.0, 0.0, 0.0};
    mapT.scale = glm::vec3{1, 1, 1};
    spawn(reg, mapEnt, "models/sanctuary.glb", "");
  };
  void update(entt::registry &reg) override {
    using namespace ImGui;
    // Per-entity "is the details window open" flag, keyed by ImGui ID instead
    // of living on a component: PushID(entity) scopes GetID("inspector open")
    // to that entity, and GetStateStorage() is ImGui's own persistent-by-ID
    // storage, so the flag survives across frames without us owning it.
    ImGuiStorage *state = GetStateStorage();
    Begin("Characters");
    for (auto [e, name] : reg.view<Name>().each()) {
      Text("%s", name.text);
      PushID(static_cast<int>(entt::to_integral(e)));
      auto openId = GetID("inspector open");
      bool open = state->GetBool(openId, false);
      if (Button("Inspect")) {
        open = true;
      }
      if (open) {
        Begin(std::format("{} details", name.text).c_str(), &open);
        auto t = reg.try_get<Transform>(e);
        if (t != nullptr) {
            DragFloat3("Position", &t->position.x, 5.0f);
            DragFloat3("Rotation", &t->rotation.x, .1f);
            DragFloat3("Scale", &t->scale.x, 0.01f, 0.001f, 1000.0f);
        }
        End();
      }
      state->SetBool(openId, open);
      PopID();
    }
    End();
  }
};
