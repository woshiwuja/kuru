#include "game/plugins/heightmap.hpp"
#include "game/plugins/mesh_registry.hpp"
#include "game/plugins/plugins.hpp"
#include "game/plugins/render.hpp"
#include "lib/core/core.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

int main()
{
	try
	{
		Core app;
		// Registration order is init order: the mesh cache and the renderer must
		// exist before any plugin that builds entities.
		app.addPlugin(std::make_unique<MeshRegistryPlugin>());
		app.addPlugin(std::make_unique<RenderPlugin>());
		app.addPlugin(std::make_unique<HeightmapPlugin>());
		app.init();

		spawn(app.reg, "models/viking_room.glb", "textures/viking_room.ktx2");
		app.reg.emplace<Map>(app.reg.create()); // HeightmapPlugin builds it

		app.run();
		app.end();
	}
	catch (const std::exception &e)
	{
		fprintf(stderr, "%s\n", e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
