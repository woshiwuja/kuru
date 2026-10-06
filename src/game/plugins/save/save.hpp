#pragma once
#include "imgui.h"
#include <Kuru.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>
using namespace KR;

struct SaveMenu {
  enum Mode { Closed, Save, Load } mode = Closed;
};

struct SavePlugin : public Plugin {
  char name[64] = "";
  std::string selected;

  void init(entt::registry &r) override { r.ctx().emplace<SaveMenu>(); }

  void update(entt::registry &r) override {
    auto &mode = r.ctx().get<SaveMenu>().mode;
    if (!Core::get()->paused) {
      mode = SaveMenu::Closed;
    }
    if (mode == SaveMenu::Closed) {
      return;
    }

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
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.6f, vp->Size.y * 0.6f),
                             ImGuiCond_Always);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));
    ImGui::Begin(title, &open,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Everything above the bottom separator + button row.
    const ImGuiStyle &style = ImGui::GetStyle();
    const float footer =
        ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f + 1.0f;
    const float bodyH = ImGui::GetContentRegionAvail().y - footer;

    if (ImGui::BeginTable("saves", 2, ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthStretch, 0.4f);
      ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch,
                              0.6f);
      ImGui::TableNextColumn();
      ImGui::BeginChild("list", ImVec2(0.0f, bodyH));
      for (const auto &entry : fs::directory_iterator(dir)) {
        const std::string file = entry.path().filename().string();
        if (ImGui::Selectable(file.c_str(), file == selected)) {
          selected = file;
          // In save mode picking an existing save fills the name, to
          // overwrite it.
          snprintf(name, sizeof(name), "%s", file.c_str());
        }
      }
      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("preview", ImVec2(0.0f, bodyH));
      std::error_code ec;
      const fs::path path = dir / selected;
      if (!selected.empty() && fs::is_regular_file(path, ec)) {
        const auto written = std::chrono::floor<std::chrono::seconds>(
            std::chrono::clock_cast<std::chrono::system_clock>(
                fs::last_write_time(path, ec)));
        const std::chrono::zoned_time local{std::chrono::current_zone(),
                                            written};
        ImGui::TextUnformatted(selected.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s",
                            std::format("Saved {:%Y-%m-%d %H:%M}", local).c_str());
        ImGui::TextDisabled("%ju KB", static_cast<uintmax_t>(
                                          fs::file_size(path, ec) / 1024));
      } else {
        ImGui::TextDisabled("No save selected");
      }
      ImGui::EndChild();
      ImGui::EndTable();
    }

    ImGui::Separator();
    if (mode == SaveMenu::Save) {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
      ImGui::InputText("##name", name, sizeof(name));
      ImGui::SameLine();
      ImGui::BeginDisabled(name[0] == '\0');
      if (ImGui::Button("Save")) {
        Core::get()->save((dir / name).string().c_str());
        open = false;
      }
      ImGui::EndDisabled();
    } else {
      ImGui::BeginDisabled(selected.empty());
      if (ImGui::Button("Load")) {
        Core::get()->load((dir / selected).string().c_str());
        open = false;
      }
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected.empty());
    if (ImGui::Button("Delete")) {
      std::error_code ec;
      fs::remove(dir / selected, ec);
      selected.clear();
    }
    ImGui::EndDisabled();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    if (!open) {
      mode = SaveMenu::Closed;
    }
  }
};
