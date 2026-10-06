#pragma once
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <Kuru.h>
using namespace KR;

struct SettingsMenu {
  bool open = false;
};

struct SettingsPlugin : public Plugin {
  enum Category { General, Gameplay, Video, Audio };
  Category category = Video;

  void init(entt::registry &r) override { r.ctx().emplace<SettingsMenu>(); }

  void update(entt::registry &r) override {
    bool &open = r.ctx().get<SettingsMenu>().open;
    if (!Core::get()->paused) {
      open = false;
    }
    if (!open) {
      return;
    }
    Settings &settings = *Core::get()->settings;

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.6f, vp->Size.y * 0.6f),
                             ImGuiCond_Always);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));
    ImGui::Begin("Settings", &open,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

    // Everything above the bottom separator + button row.
    const ImGuiStyle &style = ImGui::GetStyle();
    const float footer =
        ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f + 1.0f;
    const float bodyH = ImGui::GetContentRegionAvail().y - footer;

    if (ImGui::BeginTable("settings", 2, ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("categories", ImGuiTableColumnFlags_WidthStretch,
                              0.3f);
      ImGui::TableSetupColumn("values", ImGuiTableColumnFlags_WidthStretch,
                              0.7f);
      ImGui::TableNextColumn();
      ImGui::BeginChild("categories", ImVec2(0.0f, bodyH));
      const char *names[] = {"General", "Gameplay", "Video", "Audio"};
      for (int i = 0; i < 4; i++) {
        if (ImGui::Selectable(names[i], category == i)) {
          category = static_cast<Category>(i);
        }
      }
      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("values", ImVec2(0.0f, bodyH));
      // Labels on the right of each widget take this much room.
      ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
      if (category == Video) {
        if (video(settings.video)) {
          Window &live = *Core::get()->window;
          live.mode = settings.video.window.mode;
          live.width = settings.video.window.width;
          live.height = settings.video.window.height;
          live.apply();
        }
      } else if (category == Audio) {
        audio(settings.audio);
      } else {
        ImGui::TextDisabled("Nothing here yet");
      }
      ImGui::PopItemWidth();
      ImGui::EndChild();
      ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::Button("Save")) {
      settings.save(assetPath("settings.json"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      open = false;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
  }

  // True when the window settings changed and should be applied.
  static bool video(Settings::Video &v) {
    bool changed = false;
    ImGui::SeparatorText("Window");
    int mode = v.window.mode;
    if (ImGui::Combo("Mode", &mode, "Windowed\0Fullscreen\0Borderless\0")) {
      v.window.mode = static_cast<WindowMode>(mode);
      changed = true;
    }
    // On commit, not per keystroke: typing 1920 would resize to 1, 19, 192...
    ImGui::InputInt("Width", &v.window.width, 0);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputInt("Height", &v.window.height, 0);
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Graphics");
    quality("Textures", v.graphics.textures.quality);
    quality("Shadows", v.graphics.shadows.quality);
    if (ImGui::Checkbox("VSync", &v.graphics.vsync)) {
      // The swapchain picks its present mode on creation: rebuild it.
      Core::get()->graphics->framebufferResized = true;
    }
    return changed;
  }

  static void audio(Settings::Audio &a) {
    volume("Master", a.master_volume);
    volume("Sound effects", a.sound_effects_volume);
    volume("Music", a.music_volume);
    ImGui::InputTextWithHint("Output device", "System default",
                             &a.output_device);
  }

  static void quality(const char *label, Quality &q) {
    int i = q;
    if (ImGui::Combo(label, &i, "Off\0Low\0Medium\0High\0")) {
      q = static_cast<Quality>(i);
    }
  }

  static void volume(const char *label, uint32_t &v) {
    const uint32_t lo = 0;
    const uint32_t hi = 100;
    ImGui::SliderScalar(label, ImGuiDataType_U32, &v, &lo, &hi, "%u%%");
  }
};
