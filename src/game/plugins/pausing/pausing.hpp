#pragma once
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_scancode.h"
#include "imgui.h"
#include <Kuru.h>
#include "../save/save.hpp"
using namespace KR;
struct PausingPlugin : public Plugin {
  void init(entt::registry &r) override {}
  void update(entt::registry &r) override {
    const auto &frame = r.ctx().get<FrameContext>();
    auto &event = Core::get()->eventManager;
    auto &paused = Core::get()->paused;
    if (!frame.uiCapturesKeyboard && event->pressed(SDL_SCANCODE_SPACE)) {
          paused = !paused;
    }
    if (paused) {
      pauseMenu(paused, r.ctx().get<SaveMenu>().mode);
    }
  };
  void pauseMenu(bool &open, SaveMenu::Mode &saveMode) {
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("PAUSED", &open, flags);
    const ImVec2 size(120.0f, 0.0f);
    const float total =
        3 * ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().ItemSpacing.y;
    const float x = (ImGui::GetWindowWidth() - size.x) * 0.5f;
    ImGui::SetCursorPosY((ImGui::GetWindowHeight() - total) * 0.5f);
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("Load", size))
      saveMode = SaveMenu::Load;
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("Save", size))
      saveMode = SaveMenu::Save;
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("Exit", size)) {
      Core::get()->eventManager->quit = true;
    };
    ImGui::End();
  }
};
