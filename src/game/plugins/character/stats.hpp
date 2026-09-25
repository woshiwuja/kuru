#pragma  once
#include "common/common.hpp"
#include "entt/entity/fwd.hpp"
#include "imgui.h"
#include "plugin/plugin.hpp"
#include <Kuru.h>
#include <string>
#include <cstdint>
#include <algorithm>
using namespace KR;
struct Stat {
  uint32_t level = 1;
  uint32_t currentExperience = 0;
  uint32_t experienceToNext = 100;
  float modifier = 1.0;
};
inline double curve_scale(uint32_t max_level, double total_xp_at_max, double r) {
    return total_xp_at_max / std::pow(r, max_level);
}

inline uint32_t calculateRequiredXP(uint32_t from, uint32_t to,
                              uint32_t max_level = 100,
                              double total_xp_at_max = 5'000'000.0,
                              double r = 1.08) {
    assert(to >= from && to <= max_level);
    double scale = curve_scale(max_level, total_xp_at_max, r);
    double xp = scale * (std::pow(r, to) - std::pow(r, from));
    return static_cast<uint32_t>(xp);
}

template <typename T> const char *statName(const T &) {
  return cdemangle(typeid(T).name());
}
struct Strenght : public Stat {};
struct Vitality : public Stat {};
struct Agility: public Stat {};
struct Perception : public Stat {};
struct Intelligence : public Stat {};
struct Knowledge: public Stat {};

struct CharacterStats {
    Strenght strenght;
    Vitality vitality;
    Agility agility;
    Perception perception;
    Intelligence intelligence;
    Knowledge knowledge;
};

struct StatPlugin : public Plugin{
   void init(entt::registry &r) override {
       registerComponent<CharacterStats>();
   }
   void update(entt::registry &r) override {
       using namespace ImGui;
       Begin("Stats");
       // Stats are in the view, not fetched with get(): the inspector can add
       // Character/FirstName alone, and get<Strenght> asserts on such an entity.
       for (auto [e,stats] :
            r.view<CharacterStats, Selected>().each()) {
         auto &s = stats.strenght;
         const uint32_t required = calculateRequiredXP(s.level, std::min(s.level + 1, 100u));
         s.experienceToNext = required - std::min(s.currentExperience, required);
         const float percentage = required ? static_cast<float>(s.currentExperience) / required : 0.0f;
         s.currentExperience++;
         if(s.currentExperience == required){
             s.level++;
             s.currentExperience = 0;
         }
         PushID(entt::to_integral(e));
         Text("name: %s", statName(s));
         Text("level: %d", s.level);
         Text("current: %d", s.currentExperience);
         Text("required: %d", s.experienceToNext);
         ProgressBar(percentage, ImVec2{200,30}, "EXP");
         PopID();
       }
       End();
   };
};
