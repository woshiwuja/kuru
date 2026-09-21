#pragma once
#include "Kuru.h"
#include "SDL3/SDL_events.h"
#include "default.hpp"
#include "entt/entity/fwd.hpp"
#include "plugin/plugin.hpp"
#include <cstdint>
using namespace KR;
struct Inventory {
  uint32_t width = 100;
  uint32_t height = 100;
};
struct InventoryPlugin : public KR::Plugin {
    void init(entt::registry &r) override {
        registerComponent<Inventory>();
    }
  void update(entt::registry &r) override {
    auto &event = Core::get()->eventManager;
    if (event->down(SDL_SCANCODE_I)) {
      UI(r);
    }
  }
  void UI(entt::registry &r) {
    using namespace ImGui;
    Begin("Inventory");
    for (auto [e, inventory] : r.view<Selected, Inventory>().each()) {
      if (ImGui::BeginTable("Inventory", inventory.width)) {
        for (int i = 0; i < inventory.width * inventory.height; i++) {
          if (i % inventory.width == 0)
            ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(i % inventory.width);
          ImGui::PushID(i);
          ImGui::Button("", ImVec2(96, 96));
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    }
    End();
  };
};
