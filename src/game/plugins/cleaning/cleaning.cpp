#include "cleaning.hpp"
void CleaningPlugin::update(entt::registry &r){
    for(auto [e]:r.view<PendingClean>(entt::exclude<JPH::BodyID>).each()){
        r.destroy(e);
    }
}
