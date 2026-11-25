#pragma once
#include "plugin/default_plugins/transform/transform.hpp"
#include <Kuru.h>
#include <algorithm>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace KR;

struct ThirdPersonCamera {
  entt::entity target = entt::null;
  float yaw = -90.0f;   // degrees around +Y
  float pitch = -20.0f; // degrees, negative looks down
  float distance = 1.5f;
  float height = 1.0f;
  float shoulder = 2.5f; // meters the aim point sits right of the tar
  float sensitivity = 0.2f;
  float zoomSpeed = 0.1f;
  float fov = 60.0f;

  [[nodiscard]] glm::vec3 front() const {
    const float y = glm::radians(yaw);
    const float p = glm::radians(pitch);
    return {std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)};
  }
};

struct CameraPlugin : public Plugin {
  bool captured = true;

  CameraPlugin() { registerComponent<ThirdPersonCamera>(); }

  void update(entt::registry &reg) override {
    auto &frame = reg.ctx().get<FrameContext>();
    const Core *core = Core::get();
    const EventManager &input = *core->eventManager;
    if (input.pressed(SDL_SCANCODE_ESCAPE)) {
      captured = false;
    }
    if (input.down(SDL_BUTTON_LEFT) && !frame.uiCapturesMouse) {
      captured = true;
    }
    SDL_SetWindowRelativeMouseMode(core->window->window, captured);
    if (captured) {
      crosshair();
    }
    for (auto [e, cam] : reg.view<ThirdPersonCamera>().each()) {
      if (captured) {
        cam.yaw += input.mouseDeltaX * cam.sensitivity;
        cam.pitch -= input.mouseDeltaY * cam.sensitivity;
      }
      cam.pitch = std::clamp(cam.pitch, -85.0f, 85.0f);
      cam.distance = std::clamp(cam.distance, 1.0f, 50.0f);

      glm::vec3 pivot{0.0f};
      if (const auto *t = reg.try_get<Transform>(cam.target)) {
        pivot = t->position;
      }
      pivot.y += cam.height;
      const glm::vec3 up(0.0f, 1.0f, 0.0f);
      pivot += glm::normalize(glm::cross(cam.front(), up)) * cam.shoulder;
      const glm::vec3 eye = pivot - cam.front() * cam.distance;

      frame.view = glm::lookAt(eye, pivot, glm::vec3(0.0f, 1.0f, 0.0f));
      frame.proj = glm::perspective(glm::radians(cam.fov),
                                    static_cast<float>(frame.extent.width) /
                                        static_cast<float>(frame.extent.height),
                                    0.1f, 1000.0f);
      frame.proj[1][1] *= -1;
      frame.projNoReverseZ = frame.proj;
      static const glm::mat4 REVERSE_Z(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0,
                                       0, 1, 1);
      frame.proj = REVERSE_Z * frame.proj;
    }
  }

  static void crosshair() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImVec2 c{vp->Pos.x + vp->Size.x * 0.5f,
                   vp->Pos.y + vp->Size.y * 0.5f};
    constexpr float size = 8.0f;
    constexpr ImU32 color = IM_COL32(255, 255, 255, 200);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    draw->AddLine({c.x - size, c.y}, {c.x + size, c.y}, color, 2.0f);
    draw->AddLine({c.x, c.y - size}, {c.x, c.y + size}, color, 2.0f);
  }
};
