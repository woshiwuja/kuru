#pragma once
#include "entt/entt.hpp"
#include "glm/glm.hpp"
#include "imgui.h"
#include "ImGuizmo.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL // glm/gtx/matrix_decompose.hpp
#endif
#include <glm/gtx/matrix_decompose.hpp>
#include <Kuru.h>

using namespace KR;


struct Transform {
	KR::vec3 position = {0.0f, 0.0f, 0.0f};
	glm::quat rotation = {1.0f,0.0f, 0.0f, 0.0f};
	glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
	glm::mat4 matrix() const;
};

inline bool dragVec3(const char *label, KR::vec3 &v, float speed = 1.0f) {
  return ImGui::DragScalarN(label, ImGuiDataType_Double, &v.x, 3, speed);
}

inline bool dragRotation(const char *label, glm::quat &rotation) {
  glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
  if (!ImGui::DragFloat3(label, &euler.x, 1.0f)) return false;
  rotation = glm::quat(glm::radians(euler));
  return true;
}

struct GlobalTransform: public Transform{};
struct LocalTransform: public Transform{};


struct TransformPlugin : public Plugin {
    void init(entt::registry &r) override {
        registerComponent<Transform>();
        r.ctx().emplace<GlobalTransform>();
        r.ctx().emplace<LocalTransform>();
    };
    ImGuizmo::OPERATION gizmoOp = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE gizmoMode = ImGuizmo::WORLD;

    void update(entt::registry &r) override {
        UI(r);
        gizmo(r);
    };

    // Gizmo on the Selected entity. Runs before the character controller
    // (main.cpp order), so claiming the mouse here keeps a drag from also
    // being a click in the world.
    void gizmo(entt::registry &r) {
        auto &frame = r.ctx().get<FrameContext>();
        auto view = r.view<Selected, Transform>();
        if (view.begin() == view.end())
            return;
        auto &t = view.get<Transform>(*view.begin());

        glm::mat4 proj = frame.skyRayProj;
        proj[1][1] *= -1;
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
        ImGuizmo::SetRect(vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y);

        glm::mat4 model = t.matrix();
        if (ImGuizmo::Manipulate(&frame.view[0][0], &proj[0][0], gizmoOp,
                                 gizmoMode, &model[0][0])) {
            // Write back only what this operation changes, so decompose
            // round-off doesn't creep into the other two.
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(model, scale, rotation, translation, skew, perspective);
            if (gizmoOp == ImGuizmo::TRANSLATE) t.position = {translation.x, translation.y, translation.z};
            if (gizmoOp == ImGuizmo::ROTATE) t.rotation = glm::normalize(rotation);
            if (gizmoOp == ImGuizmo::SCALE) t.scale = scale;
        }
        if (ImGuizmo::IsOver() || ImGuizmo::IsUsing())
            frame.uiCapturesMouse = true;
    }

    void UI(entt::registry &r){
        using namespace ImGui;
        if (ImGui::Begin("Transforms")) {
          int op = gizmoOp == ImGuizmo::TRANSLATE ? 0 : gizmoOp == ImGuizmo::ROTATE ? 1 : 2;
          if (RadioButton("Translate", &op, 0) | (SameLine(), RadioButton("Rotate", &op, 1)) |
              (SameLine(), RadioButton("Scale", &op, 2)))
            gizmoOp = op == 0 ? ImGuizmo::TRANSLATE : op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
          bool local = gizmoMode == ImGuizmo::LOCAL;
          if (Checkbox("Local space", &local))
            gizmoMode = local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
          Separator();
          for (auto [entity, transform] : r.view<Transform>().each()) {
            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            if (ImGui::CollapsingHeader(
                    ("entity " + std::to_string(entt::to_integral(entity)))
                        .c_str())) {
              dragVec3("position", transform.position, 5.0f);
              dragRotation("rotation", transform.rotation);
              ImGui::DragFloat3("scale", &transform.scale.x, 0.01f, 0.001f, 1000.0f);
              if (ImGui::Button("center at origin")) {
                transform.position = {0.0f, 0.0f, 0.0f};
              }
            }
            ImGui::PopID();
          }
        }
        ImGui::End();
    }
};
