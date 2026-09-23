#pragma once
#include "entt/entt.hpp"
#include "plugin/plugin.hpp"
#include "stats.hpp"
#include <format>
#include <string>
#include <Kuru.h>
#include "name.hpp"
#include <imgui.h>

using namespace KR;
struct Character {};

struct CharacterPlugin : public Plugin {
  void init(entt::registry &r) override {
    registerComponent<Character>();
  }
  void update(entt::registry &r) override { UI(r); }
  void UI(entt::registry &r) {
    using namespace ImGui;
    // Stats are in the view, not fetched with get(): the inspector can add
    // Character/FirstName alone, and get<Strenght> asserts on such an entity.
    for (auto [e, name, s, v] :
         r.view<Character, Name,  Strenght, Vitality>().each()) {
      // Names can be empty or shared; ImGui wants a non-empty unique title.
      Begin(std::format("{} {}###character{}", name.fname.c_str(),
                        name.lname.c_str(), entt::to_integral(e))
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
    r.emplace<Name>(e, Name{.fname= "New",.lname="Name"});
    r.emplace<Strenght>(e, Stat{.experienceToNext = calculateRequiredXP(1, 2)});
    r.emplace<Vitality>(e, Stat{.experienceToNext = calculateRequiredXP(1, 2)});
    return e;
  }
};
