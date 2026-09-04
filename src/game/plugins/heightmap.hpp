#pragma once
#include "../../lib/plugin/plugin.hpp"
#include <entt/entt.hpp>
#include <string>

// Tag an entity with this and the plugin turns it into terrain: it loads the
// heightmap, builds the grid mesh and hands the entity to the renderer.
struct Map {
	std::string path        = "textures/Heightmap.png";
	std::string texture     = "textures/viking_room.ktx2";
	float       cellSize    = 0.08f;
	float       heightScale = 2.0f;
};

struct HeightmapPlugin : public Plugin {
	void run(entt::registry &reg) override;
};
