#include "game/plugins/camera.hpp"
#include "game/plugins/heightmap.hpp"
#include "game/plugins/mesh_registry.hpp"
#include "game/plugins/plugins.hpp"
#include "game/plugins/render.hpp"
#include "game/plugins/ui.hpp"
#include "lib/core/core.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

int main()
{
	try
	{
		Core app;
		// Registration order is both init and run order: the mesh cache and the
		// camera come before the renderer, which reads the view they set up;
		// HeightmapPlugin needs the renderer to already be in the registry.
		app.addPlugin(std::make_unique<MeshRegistryPlugin>());
		app.addPlugin(std::make_unique<CameraPlugin>());
		app.addPlugin(std::make_unique<RenderPlugin>());
		app.addPlugin(std::make_unique<HeightmapPlugin>());
		app.addPlugin(std::make_unique<UiPlugin>()); // last: draws over the scene
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
