#include "material.hpp"
#include "common/material.hpp"
#include "entt/entity/fwd.hpp"
using namespace ImGui;

void materialEditor(Material &m) {
  ColorEdit4("Color", &m.color.x);
  SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
  SliderFloat("Roughness", &m.roughness, 0.04f, 1.0f);
  Checkbox("Alpha mask", &m.alphaMask);
  if (m.alphaMask){
      ImGui::SliderFloat("Alpha cutoff", &m.alphaCutoff, 0.0f, 1.0f);
  }
  Text("baseColor: %s", m.baseColor ? "set" : "-");
  Text("metallicRoughness: %s", m.metallicRoughness ? "set" : "-");
  Text("normal: %s", m.normal ? "set" : "-");
  Text("occlusion: %s", m.occlusion ? "set" : "-");
  Text("emissive: %s", m.emissive ? "set" : "-");
}

void MaterialPlugin::UI(Material &m) {
  Begin("Materials");
  materialEditor(m);
  End();
}

void MaterialPlugin::update(entt::registry &r){
        for (auto [e, mat] : r.view<MaterialRef, Selected>().each()){
            UI(*mat.material);
        }
}
