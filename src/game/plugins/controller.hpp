#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/TransformedShape.h"
#include "SDL3/SDL_mouse.h"
#include "camera.hpp"
#include "character.hpp"
#include "../../lib/plugin/plugin.hpp"
#include "../../lib/core/core.hpp"
#include "physics.hpp"
#include "transform.hpp"

#include "entt/entity/fwd.hpp"
entt::entity pick(entt::registry& reg, const JPH::RRayCast& r) {
    entt::entity best = entt::null;
    float bestT = INFINITY;

    for (auto [e, xf, b] : reg.view<Transform, Bounds>().each()) {
        float t = rayAabb(toObject(r, glm::inverse(xf.model)), b.lo, b.hi);
        if (t >= 0.f && t < bestT) { bestT = t; best = e; }
    }

    // Closest collider wins; non-selectable closest => occluded, nothing picked.
    return (best != entt::null && reg.all_of<Selectable>(best)) ? best : entt::null;
}
struct CharacterControllerPlugin : public Plugin{
    void init(entt::registry &r) override {
    }
    void update(entt::registry &r ) override {
        const auto &input = *Core::get()->eventManager;
        if (input.down(SDL_BUTTON_LEFT)) {
            for(auto [e,camera] : r.view<MainCamera>().each()){
                JPH::RRayCast ray{JPH::RVec3(glmVecToJPH(camera.position()), dir * kPickRange};
                pick(reg,ray );
            }
        }
    }
};
