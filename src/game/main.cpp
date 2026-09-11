#include <Kuru.h>
#include "plugins/camera.hpp"
#include "plugins/lighting.hpp"
#include "plugins/map.hpp"
#include "plugins/mesh_registry.hpp"
#include "plugins/navigation.hpp"
#include "plugins/physics.hpp"
#include "plugins/render.hpp"
#include "plugins/ui.hpp"
#include "plugins/default.hpp"
#include "plugins/physics.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace KR;

int main()
{
	try
	{
		Core app;
		app.addPlugin(std::make_unique<PhysicsPlugin>());
		app.addPlugin(std::make_unique<MeshRegistryPlugin>());
		app.addPlugin(std::make_unique<CameraPlugin>());
		app.addPlugin(std::make_unique<LightingPlugin>());
		app.addPlugin(std::make_unique<RenderPlugin>());
		app.addPlugin(std::make_unique<TransformPlugin>());
		app.addPlugin(std::make_unique<DefaultPlugin>());
		app.addPlugin(std::make_unique<UiPlugin>());
		app.addPlugin(std::make_unique<MapPlugin>());
		app.addPlugin(std::make_unique<NavigationPlugin>());
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
