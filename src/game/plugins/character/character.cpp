
#include "character.hpp"
void CharacterPlugin::UI(entt::registry &r){
  using namespace ImGui;
  Begin("Characters");
  if (Button("New Character"))
    newCharacter(r);
  End();
}

void CharacterPlugin::update(entt::registry &r) {
    UI(r);
}

entt::entity CharacterPlugin::newCharacter(entt::registry &r){
  auto e = r.create();
  r.emplace<Character>(e);
  r.emplace<Name>(e, Name{.fname= "New",.lname="Name"});
  return e;
}
