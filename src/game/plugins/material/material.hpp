#pragma once
#include <Kuru.h>
using namespace KR;
struct MaterialRef {
  // The inspector default-constructs this, and every reader derefs it.
  std::shared_ptr<Material> material = std::make_shared<Material>();
  glm::vec4 params{0.0f};
};
struct MaterialPlugin : public Plugin{
    MaterialPlugin() {
        registerComponent<MaterialRef>();
        registerComponent<Material>();
    }
    void update(entt::registry &r ) override {
        for (auto [e, mat] : r.view<MaterialRef, Selected>().each()){
            UI(r,*mat.material);
        }
    }
    void UI(entt::registry &r, Material &m){
        ImGui::Begin("Materials");
            materialEditor(m);
        ImGui::End();
    }
    void materialEditor(Material &m){
        ImGui::ColorEdit4("Color", &m.color.x);
        ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &m.roughness, 0.04f,
                           1.0f);
        ImGui::Checkbox("Alpha mask", &m.alphaMask);
        if (m.alphaMask)
          ImGui::SliderFloat("Alpha cutoff", &m.alphaCutoff, 0.0f, 1.0f);
        ImGui::Text("baseColor: %s", m.baseColor ? "set" : "-");
        ImGui::Text("metallicRoughness: %s", m.metallicRoughness ? "set" : "-");
        ImGui::Text("normal: %s", m.normal ? "set" : "-");
        ImGui::Text("occlusion: %s", m.occlusion ? "set" : "-");
        ImGui::Text("emissive: %s", m.emissive ? "set" : "-");
    }
};
