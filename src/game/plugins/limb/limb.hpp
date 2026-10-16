#pragma once
#include "../character/character.hpp"
#include "../render/render.hpp"
#include "entt/entity/fwd.hpp"
#include <Kuru.h>
#include <cstring>
#include <vector>

// A Character's glTF node named "limb_*" (limb_arm_l, limb_head...): it
// follows the character as a ModelNode until its health runs out.
struct Limb {
  float health = 100.0f;
};

struct LimbPlugin : public Plugin {
  LimbPlugin() {
    registerComponent<Limb>();
  }
  void init(entt::registry &r) override {}
  void update(entt::registry &r) override {

    for (auto [e, node] : r.view<ModelNode>(entt::exclude<Limb>).each()) {
      if (r.valid(node.root) && r.all_of<Character>(node.root) &&
          std::strncmp(node.name, "limb_", 5) == 0) {
        r.emplace<Limb>(e);
      }
    }

    std::vector<entt::entity> severed;
    for (auto [e, limb, node] : r.view<Limb, ModelNode>().each()) {
      if (limb.health <= 0.0f) {
        severed.push_back(e);
      }
    }

    for (entt::entity e : severed) {
      sever(r, e);
    }
  }

  static void sever(entt::registry &r, entt::entity e) {
    detach(r, e);
    r.remove<ModelNode>(e);
  }
};
