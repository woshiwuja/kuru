#pragma once
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_scancode.h"
#include "imgui.h"
#include <Kuru.h>
#include "../save/save.hpp"
#include "../settings/settings.hpp"
using namespace KR;

struct PausingPlugin : public Plugin {
  void init(entt::registry &r) override {}
  void update(entt::registry &r) override {
    bool &escmenu = Core::get()->frozen;
    const auto &frame = r.ctx().get<FrameContext>();
    auto &event = Core::get()->eventManager;
    auto &paused = Core::get()->paused;
    if (!frame.uiCapturesKeyboard && !escmenu &&
        event->pressed(SDL_SCANCODE_SPACE)) {
      paused = !paused;
    } else if (!frame.uiCapturesKeyboard &&
               event->pressed(SDL_SCANCODE_ESCAPE)) {
      escmenu = !escmenu;
      paused = escmenu;
    }
    if (escmenu && paused) {
      escMenu(escmenu, r.ctx().get<SaveMenu>().mode,
              r.ctx().get<SettingsMenu>().open);
    }
    else if (paused) {
      pauseMenu(paused);
    }
  };
  void escMenu(bool &open, SaveMenu::Mode &saveMode, bool &settingsOpen) {
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 32.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 16.0f));
    ImGui::Begin("ESC", &open, flags);
    // Width in font units, so it follows the font size above.
    const ImVec2 size(ImGui::GetFontSize() * 10.0f, 0.0f);
    if (ImGui::Button("Load", size)) {
      saveMode = SaveMenu::Load;
    }
    if (ImGui::Button("Save", size)) {
      saveMode = SaveMenu::Save;
    }
    if (ImGui::Button("Settings", size)) {
      settingsOpen = true;
    }
    if (ImGui::Button("Exit", size)) {
      Core::get()->eventManager->quit = true;
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
  }
void pauseMenu(bool &open){
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::Begin("Pause",&open,flags);
  ImGui::Text("P A U S E D");
  ImGui::End();
}
};
