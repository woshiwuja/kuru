
#include "character.hpp"
#include "Jolt/Physics/Body/BodyID.h"
#include "entt/entity/fwd.hpp"
#include "../render/render.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <vector>
void CharacterPlugin::UI(entt::registry &r){
  using namespace ImGui;
  Begin("Characters");
  if (Button("New Character"))
    newCharacter(r);
  End();
}

void clean(entt::registry &r){
    std::vector<entt::entity> fallen;
    for(auto [e,t,b] : r.view<Character, Transform, JPH::BodyID>().each()){
        if (t.position.y <= -1000){
            fallen.push_back(e);
        }
    }
    // The body goes with the BodyID: see PhysicsPlugin::removeBody.
    for (auto e : fallen) {
        renderer(r).despawn(r, e);
    }
}
void CharacterPlugin::update(entt::registry &r) {
    clean(r);
    UI(r);
}

entt::entity CharacterPlugin::newCharacter(entt::registry &r){
  auto e = r.create();
  r.emplace<Character>(e);
  r.emplace<Name>(e, Name{.fname= "New",.lname="Name"});
  return e;
}
