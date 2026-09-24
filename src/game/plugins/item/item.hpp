#include "entt/entity/fwd.hpp"
#include "plugin/plugin.hpp"
#include <cstdint>
#include <string>
#include <filesystem>
struct Item {
    std::string name = "Item";
    std::string description = "An item";
    std::filesystem::path model = "models/default_item.glb";
    std::filesystem::path icon = "images/default_item.jpg";
    uint32_t amount = 1;
    float weight = 0.1; //kg
};
struct Weapon{};
struct Armor{};
struct Misc{};
struct Food{};

struct ItemPlugin : public KR::Plugin {
    void init(entt::registry &r) {
        KR::registerComponent<Item>();
        KR::registerComponent<Weapon>();
        KR::registerComponent<>();
        KR::registerComponent<Item>();
        KR::registerComponent<Item>();
    }
}
