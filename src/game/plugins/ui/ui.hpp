#pragma once
#include "imgui.h"
#include <Kuru.h>
#include <entt/entt.hpp>

using namespace KR;

struct Color{
  float r,g,b,a;
  operator ImVec4() const {
    return ImVec4(r,g,b,a);
  }
  constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
  static const Color Red, Green, Blue, Yellow, Cyan, Magenta, White, Black,
      Gray, Transparent;
};
inline constexpr Color Color::Red{1.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color Color::Green{0.0f, 1.0f, 0.0f, 1.0f};
inline constexpr Color Color::Blue{0.0f, 0.0f, 1.0f, 1.0f};
inline constexpr Color Color::Yellow{1.0f, 1.0f, 0.0f, 1.0f};
inline constexpr Color Color::Cyan{0.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color Color::Magenta{1.0f, 0.0f, 1.0f, 1.0f};
inline constexpr Color Color::White{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color Color::Black{0.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color Color::Gray{0.5f, 0.5f, 0.5f, 1.0f};
inline constexpr Color Color::Transparent{0.0f, 0.0f, 0.0f, 0.0f};
struct UIPlugin : public Plugin {
  void init(entt::registry &reg) override;
  void start(entt::registry &reg) override;
  void update(entt::registry &reg) override;
  void end(entt::registry &reg) override;
};
ImFont *findFont(const char *name);

void setStyle(ImGuiStyle& style);
