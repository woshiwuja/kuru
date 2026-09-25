#pragma once
#include "entt/entt.hpp"
#include "plugin/plugin.hpp"
#include "stats.hpp"
#include <format>
#include <string>
#include <Kuru.h>
#include "../name/name.hpp"
#include <imgui.h>

using namespace KR;
struct Character {
};

struct CharacterPlugin : public Plugin {
  void init(entt::registry &r) override {
    registerComponent<Character>();
  }
  void update(entt::registry &r) override { UI(r); }
  void UI(entt::registry &r) {
    using namespace ImGui;
    Begin("Characters");
    if (Button("New Character"))
      newCharacter(r);
    End();
  }

  static entt::entity newCharacter(entt::registry &r) {
    auto e = r.create();
    r.emplace<Character>(e);
    r.emplace<Name>(e, Name{.fname= "New",.lname="Name"});
    return e;
  }
};
