#pragma once
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_scancode.h"
#include "imgui.h"
#include <Kuru.h>
using namespace KR;
struct PausingPlugin : public Plugin {
    void init(entt::registry &r) override {
    }
  void update(entt::registry &r) override {
    const auto &frame = r.ctx().get<FrameContext>();
    auto &event = Core::get()->eventManager;
    if (!frame.uiCapturesKeyboard && (event->pressed(SDL_SCANCODE_P) || event->pressed(SDL_SCANCODE_ESCAPE))) {
        if (Core::get()->paused){
            Core::get()->paused = false;
        }
        else{
            Core::get()->paused = true;
        }
    }
    if (Core::get()->paused){
        pauseMenu();
    }
  };
  void pauseMenu(){
      ImGui::Begin("PAUSED");
      ImGui::Button("Load");
      ImGui::Button("Save");
      if(ImGui::Button("Exit")){
          Core::get()->eventManager->quit = true;
      };
      ImGui::End();
  }
};
