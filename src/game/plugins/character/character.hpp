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
  void init(entt::registry &r) override { registerComponent<Character>(); }
  void update(entt::registry &r) override;
  void UI(entt::registry &r);
  static entt::entity newCharacter(entt::registry &r);
};
