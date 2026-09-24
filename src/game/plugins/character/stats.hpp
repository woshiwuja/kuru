#pragma  once
#include "entt/entity/fwd.hpp"
#include "plugin/plugin.hpp"
#include <Kuru.h>
#include <string>
#include <cstdint>
using namespace KR;
struct Stat {
  uint32_t level = 1;
  uint32_t currentExperience = 0;
  uint32_t experienceToNext;
  float modifier = 1.0;
};
struct Strenght : public Stat {
    std::string name = "Strenght";
};
struct Vitality : public Stat {
    std::string name = "Vitality";
};

struct StatPlugin : public Plugin{
   void init(entt::registry &r) override {
       registerComponent<Strenght>();
	   registerComponent<Vitality>();
   }
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
