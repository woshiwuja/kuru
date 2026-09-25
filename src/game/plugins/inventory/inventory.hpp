#pragma once
#include "entt/entt.hpp"
#include "plugin/plugin.hpp"
#include <Kuru.h>
#include <cstdint>
using namespace KR;
struct Inventory {
  uint32_t width = 50;
  uint32_t height = 50;
};
struct InventoryPlugin : public KR::Plugin {
    void init(entt::registry &r) override {
        registerComponent<Inventory>();
    }
  void update(entt::registry &r) override {
    using namespace ImGui;
    if (r.storage<Selected>().empty()) return;
    ImGuiStorage *state = GetStateStorage();
    const ImGuiID openId = GetID("inventory open");
    bool open = state->GetBool(openId, false);
    const auto &frame = r.ctx().get<FrameContext>();
    auto &event = Core::get()->eventManager;
    if (!frame.uiCapturesKeyboard && event->pressed(SDL_SCANCODE_I)) {
      open = !open;
    }
    if (open){UI(r, open);}
    state->SetBool(openId, open);
  }
  void UI(entt::registry &r, bool &open) {
    using namespace ImGui;
    if (Begin("Inventory", &open)) {
    for (auto [e, inventory] : r.view<Selected, Inventory>().each()) {
      if (BeginTable("Inventory", inventory.width)) {
        for (int i = 0; i < inventory.width * inventory.height; i++) {
          if (i % inventory.width == 0){
              TableNextRow();
              TableSetColumnIndex(i % inventory.width);
              PushID(i);
              Button("", vec2(64, 64));
              PopID();
          }
        }
        EndTable();
      }
    }
    }
    End();
  };
};
