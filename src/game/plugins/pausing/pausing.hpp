#pragma once
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_scancode.h"
#include "imgui.h"
#include <Kuru.h>
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
      pauseMenu(paused);
    }
  };
  void pauseMenu(bool &open) {
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("PAUSED", &open, flags);
    const ImVec2 size(120.0f, 0.0f);
    const float total =
        3 * ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().ItemSpacing.y;
    const float x = (ImGui::GetWindowWidth() - size.x) * 0.5f;
    ImGui::SetCursorPosY((ImGui::GetWindowHeight() - total) * 0.5f);
    ImGui::SetCursorPosX(x);
    ImGui::Button("Load", size);
    ImGui::SetCursorPosX(x);
    ImGui::Button("Save", size);
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("Exit", size)) {
      Core::get()->eventManager->quit = true;
    };
    ImGui::End();
  }
};
