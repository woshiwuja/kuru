#pragma once
#include "imgui.h"
#include <Kuru.h>
#include <filesystem>
#include <string>
using namespace KR;

// Which list the save window shows. PausingPlugin's buttons set it.
struct SaveMenu {
  enum Mode { Closed, Save, Load } mode = Closed;
};

struct SavePlugin : public Plugin {
  char name[64] = "";

  void init(entt::registry &r) override { r.ctx().emplace<SaveMenu>(); }

  void update(entt::registry &r) override {
    auto &mode = r.ctx().get<SaveMenu>().mode;
    if (!Core::get()->paused)
      mode = SaveMenu::Closed;
    if (mode == SaveMenu::Closed)
      return;

    namespace fs = std::filesystem;
    // Next to the exe, like every other asset: a CWD-relative dir moves with
    // wherever the game was launched from.
    const fs::path dir = assetPath("saves");
    fs::create_directories(dir);

    bool open = true;
    const char *title = "Load game";
    if (mode == SaveMenu::Save) {
      title = "Save game";
    }
    ImGui::Begin(title, &open,
                 ImGuiWindowFlags_AlwaysAutoResize);
    if (mode == SaveMenu::Save) {
      ImGui::InputText("name", name, sizeof(name));
      ImGui::BeginDisabled(name[0] == '\0');
      if (ImGui::Button("Save")) {
        Core::get()->save((dir / name).string().c_str());
        open = false;
      }
      ImGui::EndDisabled();
      ImGui::Separator();
    }
    for (const auto &entry : fs::directory_iterator(dir)) {
      const std::string file = entry.path().filename().string();
      if (!ImGui::Selectable(file.c_str()))
        continue;
      if (mode == SaveMenu::Save) {
        // Clicking an existing save picks its name, to overwrite it.
        snprintf(name, sizeof(name), "%s", file.c_str());
      } else {
        Core::get()->load(entry.path().string().c_str());
        open = false;
      }
    }
    ImGui::End();
    if (!open)
      mode = SaveMenu::Closed;
  }
};
