#include "ui.hpp"
#include <Kuru.h>
#include "entt/entity/fwd.hpp"
#include "imgui.h"
#include "ImGuizmo.h"
#include "../render/render.hpp"

using namespace KR;


void UIPlugin::init(entt::registry &reg){
    auto &style = ImGui::GetStyle();
    setStyle(style);
}
void UIPlugin::start(entt::registry &reg) {
  for (const SDL_Event &event : Core::get()->eventManager->events) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGuizmo::BeginFrame();

  ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                               ImGuiDockNodeFlags_PassthruCentralNode);
  ImGuiIO &io = ImGui::GetIO();
}

void UIPlugin::end(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), **frame.commandBuffer);

  const ImGuiIO &io = ImGui::GetIO();
  frame.uiCapturesMouse = io.WantCaptureMouse;
  frame.uiCapturesKeyboard = io.WantCaptureKeyboard;
}

void UIPlugin::update(entt::registry &reg) {
}

ImFont *findFont(const char *name){
  for (ImFont *font : ImGui::GetIO().Fonts->Fonts){
      if (std::strcmp(font->GetDebugName(), name) == 0){
          return font;
      }
  }
  return nullptr;
}

void setStyle(ImGuiStyle &style){
    style.Colors[ImGuiCol_Text]                  = Color::White.withAlpha(0.78f);
    style.Colors[ImGuiCol_TextDisabled]          = Color::White.withAlpha(0.28f);
    style.Colors[ImGuiCol_WindowBg]              = Color::Black;
    style.Colors[ImGuiCol_Border]                = Color::Blue.withAlpha(0.00f);
    style.Colors[ImGuiCol_BorderShadow]          = Color::Transparent;
    style.Colors[ImGuiCol_FrameBg]               = Color::Black;
    style.Colors[ImGuiCol_FrameBgHovered]        = Color::Red.withAlpha(0.78f);
    style.Colors[ImGuiCol_FrameBgActive]         = Color::Red;
    style.Colors[ImGuiCol_TitleBg]               = Color::Black;
    style.Colors[ImGuiCol_TitleBgCollapsed]      = Color::Black.withAlpha(0.75f);
    style.Colors[ImGuiCol_TitleBgActive]         = Color::Red;
    style.Colors[ImGuiCol_MenuBarBg]             = Color::Black.withAlpha(0.47f);
    style.Colors[ImGuiCol_ScrollbarBg]           = Color::Black;
    style.Colors[ImGuiCol_ScrollbarGrab]         = Color::Black;
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = Color::Red.withAlpha(0.78f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = Color::Red;
    style.Colors[ImGuiCol_CheckMark]             = Color::Gray;
    style.Colors[ImGuiCol_SliderGrab]            = Color::Gray.withAlpha(0.14f);
    style.Colors[ImGuiCol_SliderGrabActive]      = Color::Red;
    style.Colors[ImGuiCol_Button]                = Color::Gray.withAlpha(0.14f);
    style.Colors[ImGuiCol_ButtonHovered]         = Color::Red.withAlpha(0.86f);
    style.Colors[ImGuiCol_ButtonActive]          = Color::Red;
    style.Colors[ImGuiCol_Header]                = Color::Red.withAlpha(0.76f);
    style.Colors[ImGuiCol_HeaderHovered]         = Color::Red.withAlpha(0.86f);
    style.Colors[ImGuiCol_HeaderActive]          = Color::Red;
    style.Colors[ImGuiCol_Separator]             = Color::Black;
    style.Colors[ImGuiCol_SeparatorHovered]      = Color::Red.withAlpha(0.78f);
    style.Colors[ImGuiCol_SeparatorActive]       = Color::Red;
    style.Colors[ImGuiCol_ResizeGrip]            = Color::Gray.withAlpha(0.04f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = Color::Red.withAlpha(0.78f);
    style.Colors[ImGuiCol_ResizeGripActive]      = Color::Red;
    style.Colors[ImGuiCol_PlotLines]             = Color::White.withAlpha(0.63f);
    style.Colors[ImGuiCol_PlotLinesHovered]      = Color::Red;
    style.Colors[ImGuiCol_PlotHistogram]         = Color::White.withAlpha(0.63f);
    style.Colors[ImGuiCol_PlotHistogramHovered]  = Color::Red;
    style.Colors[ImGuiCol_TextSelectedBg]        = Color::Red.withAlpha(0.43f);
    style.Colors[ImGuiCol_PopupBg]               = Color::Black.withAlpha(0.90f);
    style.Colors[ImGuiCol_ModalWindowDimBg]  = Color::Black.withAlpha(0.73f);
}
