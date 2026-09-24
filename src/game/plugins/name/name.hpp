#include "imgui.h"
#include "entt/entt.hpp"
#include "plugin/plugin.hpp"
#include "../transform/transform.hpp"
#include "../ui/ui.hpp"
#include "physics/raycast.hpp"
struct Name {
  std::string fname = "fname";
  std::string nname = "nname";
  std::string lname = "lname";
};
struct NamePlugin : public KR::Plugin {
  void init(entt::registry &r) override {
      registerComponent<Name>();
  }
  void update(entt::registry &r) override {
    const auto &frame = r.ctx().get<FrameContext>();
    auto bg = ImGui::GetBackgroundDrawList();
    ImGui::PushFont(findFont("Jersey10-Regular.ttf"), 24.0f);
    for (auto [e, name, t] : r.view<Name, Transform>().each()) {
      glm::vec2 pixel;
      if (!worldToScreen(frame, t.position+KR::vec3{0,2,0}, pixel)) {
        continue;
      }
      bg->AddText({pixel.x, pixel.y}, IM_COL32(255, 255, 255, 255),
                  name.fname.c_str());
    }
    ImGui::PopFont();
  }
};
