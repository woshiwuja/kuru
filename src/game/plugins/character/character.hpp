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
struct Character {};

struct CharacterPlugin : public Plugin {
  CharacterPlugin(){
    registerComponent<Character>();
  }
  void init(entt::registry &r) override {}
  void update(entt::registry &r) override;
  void UI(entt::registry &r);
  static entt::entity newCharacter(entt::registry &r);
};

void clean(entt::registry &r);
