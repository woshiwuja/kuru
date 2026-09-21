#pragma once
#include "entt/entity/fwd.hpp"
#include "plugin/plugin.hpp"
#include "stats.hpp"
#include <format>
#include <string>
#include <Kuru.h>
#include <imgui.h>

using namespace KR;
struct Character {};
struct FirstName : std::string {
  using std::string::basic_string;
};
struct LastName : std::string {
  using std::string::basic_string;
};

struct CharacterPlugin : public Plugin {
  void init(entt::registry &r) override {
    registerComponent<Character>();
    registerComponent<FirstName>();
    registerComponent<LastName>();
  }
  void update(entt::registry &r) override { UI(r); }
  void UI(entt::registry &r) {
    using namespace ImGui;
    // Stats are in the view, not fetched with get(): the inspector can add
    // Character/FirstName alone, and get<Strenght> asserts on such an entity.
    for (auto [e, first_name, last_name, s, v] :
         r.view<Character, FirstName, LastName, Strenght, Vitality>().each()) {
      // Names can be empty or shared; ImGui wants a non-empty unique title.
      Begin(std::format("{} {}###character{}", first_name.c_str(),
                        last_name.c_str(), entt::to_integral(e))
                .c_str());
      Text("name: %s", s.name.c_str());
      Text("level: %d", s.level);
      Text("current_exp: %d", s.currentExperience);
      Text("exp to next level: %d", s.experienceToNext);
      End();
    }
    // Outside the loop: emplacing Character while iterating its own pool
    // invalidates the iteration.
    Begin("Characters");
    if (Button("New Character"))
      newCharacter(r);
    End();
  }

  static entt::entity newCharacter(entt::registry &r) {
    auto e = r.create();
    r.emplace<Character>(e);
    r.emplace<FirstName>(e, "New");
    r.emplace<LastName>(e, "Character");
    r.emplace<Strenght>(e, Stat{.experienceToNext = calculateRequiredXP(1, 2)});
    r.emplace<Vitality>(e, Stat{.experienceToNext = calculateRequiredXP(1, 2)});
    return e;
  }
};
