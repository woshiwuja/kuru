#include "game/plugins/camera.hpp"
#include "game/plugins/lighting.hpp"
#include "game/plugins/map.hpp"
#include "game/plugins/mesh_registry.hpp"
#include "game/plugins/physics.hpp"
#include "game/plugins/plugins.hpp"
#include "game/plugins/render.hpp"
#include "game/plugins/ui.hpp"
#include "game/plugins/default.hpp"
#include "game/plugins/physics.hpp"
#include "lib/core/core.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

int main()
{
	try
	{
		Core app;
		app.addPlugin(std::make_unique<DefaultPlugin>());
		app.addPlugin(std::make_unique<PhysicsPlugin>());
		app.addPlugin(std::make_unique<MeshRegistryPlugin>());
		app.addPlugin(std::make_unique<CameraPlugin>());
		app.addPlugin(std::make_unique<LightingPlugin>());
		app.addPlugin(std::make_unique<RenderPlugin>());
		app.addPlugin(std::make_unique<TransformPlugin>());
		app.addPlugin(std::make_unique<UiPlugin>());
		app.addPlugin(std::make_unique<MapPlugin>());
		app.init();
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
