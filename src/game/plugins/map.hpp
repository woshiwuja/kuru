#pragma once
#include "../../lib/plugin/plugin.hpp"
#include "plugins.hpp"
#include "render.hpp"
#include "entt/entity/fwd.hpp"
#include "Recast.h"
#include <Jolt/Jolt.h>

struct Map {};
struct MapPlugin : public Plugin {
    void init(entt::registry &reg)override{
        entt::entity mapEntity = reg.create();
		spawn(reg, mapEntity, "models/testmap.glb",
		      "textures/viking_room.ktx2");
		reg.emplace<Map>(mapEntity);
		reg.emplace<Sky>(reg.create());
    }
};
