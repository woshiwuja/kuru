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

ImFont *findFont(const char *name){
  for (ImFont *font : ImGui::GetIO().Fonts->Fonts){
      if (std::strcmp(font->GetDebugName(), name) == 0){
          return font;
      }
  }
  return nullptr;
}

void setStyle(ImGuiStyle &style){
    //setColors();
    //setDefaults();
    style.Colors[ImGuiCol_Text]                  = Color::White.Alpha(0.78f);
    style.Colors[ImGuiCol_TextDisabled]          = Color::White.Alpha(0.28f);
    style.Colors[ImGuiCol_WindowBg]              = Color::Black;
    style.Colors[ImGuiCol_Border]                = Color::Blue.Alpha(0.00f);
    style.Colors[ImGuiCol_BorderShadow]          = Color::Transparent;
    style.Colors[ImGuiCol_FrameBg]               = Color::Black;
    style.Colors[ImGuiCol_FrameBgHovered]        = Color::Red.Alpha(0.78f);
    style.Colors[ImGuiCol_FrameBgActive]         = Color::Red;
    style.Colors[ImGuiCol_TitleBg]               = Color::Black;
    style.Colors[ImGuiCol_TitleBgCollapsed]      = Color::Black.Alpha(0.75f);
    style.Colors[ImGuiCol_TitleBgActive]         = Color::Red;
    style.Colors[ImGuiCol_MenuBarBg]             = Color::Black.Alpha(0.47f);
    style.Colors[ImGuiCol_ScrollbarBg]           = Color::Black;
    style.Colors[ImGuiCol_ScrollbarGrab]         = Color::Black;
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = Color::Red.Alpha(0.78f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = Color::Red;
    style.Colors[ImGuiCol_CheckMark]             = Color::Gray;
    style.Colors[ImGuiCol_SliderGrab]            = Color::Gray.Alpha(0.14f);
    style.Colors[ImGuiCol_SliderGrabActive]      = Color::Red;
    style.Colors[ImGuiCol_Button]                = Color::Gray.Alpha(0.14f);
    style.Colors[ImGuiCol_ButtonHovered]         = Color::Red.Alpha(0.86f);
    style.Colors[ImGuiCol_ButtonActive]          = Color::Red;
    style.Colors[ImGuiCol_Header]                = Color::Red.Alpha(0.76f);
    style.Colors[ImGuiCol_HeaderHovered]         = Color::Red.Alpha(0.86f);
    style.Colors[ImGuiCol_HeaderActive]          = Color::Red;
    style.Colors[ImGuiCol_Separator]             = Color::Black;
    style.Colors[ImGuiCol_SeparatorHovered]      = Color::Red.Alpha(0.78f);
    style.Colors[ImGuiCol_SeparatorActive]       = Color::Red;
    style.Colors[ImGuiCol_ResizeGrip]            = Color::Gray.Alpha(0.04f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = Color::Red.Alpha(0.78f);
    style.Colors[ImGuiCol_ResizeGripActive]      = Color::Red;
    style.Colors[ImGuiCol_PlotLines]             = Color::White.Alpha(0.63f);
    style.Colors[ImGuiCol_PlotLinesHovered]      = Color::Red;
    style.Colors[ImGuiCol_PlotHistogram]         = Color::White.Alpha(0.63f);
    style.Colors[ImGuiCol_PlotHistogramHovered]  = Color::Red;
    style.Colors[ImGuiCol_TextSelectedBg]        = Color::Red.Alpha(0.43f);
    style.Colors[ImGuiCol_PopupBg]               = Color::Black.Alpha(0.90f);
    style.Colors[ImGuiCol_ModalWindowDimBg]  = Color::Black.Alpha(0.73f);
    style.Colors[ImGuiCol_Border] = Color::White;
    style.FrameBorderSize = 2;
    style.TabBorderSize = 1;
}

bool dragVec3(const char *label, KR::vec3 &v, float speed ){
  return ImGui::DragScalarN(label, ImGuiDataType_Double, &v.x, 3, speed);
}

bool dragRotation(const char *label, glm::quat &rotation) {
  glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
  if (!ImGui::DragFloat3(label, &euler.x, 1.0f))
    return false;
  rotation = glm::quat(glm::radians(euler));
  return true;
}
