#include "entt/entity/fwd.hpp"
#include "plugin/plugin.hpp"
#include <string>
#include <Kuru.h>
#include <imgui.h>

using namespace KR;
struct Character {};
struct Selected {};
struct FirstName : std::string {
  using std::string::basic_string;
};
struct LastName : std::string {
  using std::string::basic_string;
};
struct Stat {
  int lvl = 1;
  int currentExp = 0;
  int expToNext;
};
struct Strength : public Stat {};
struct Agility : public Stat {};
struct Toughness : public Stat {};
struct Intelligence : public Stat {};
struct Perception : public Stat {};

struct CharacterPlugin : public Plugin {
  void update(entt::registry &r) override { UI(r); }
  void UI(entt::registry &r) {
    for (auto [e, first_name, last_name] :
         r.view<Character, FirstName, LastName>().each()) {
      ImGui::Begin((first_name + last_name).c_str());
      auto s = r.get<Strength>(e);
      ImGui::Text("%d", s.lvl);
      ImGui::End();
    }
  }
};
