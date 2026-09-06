#include "game/plugins/camera.hpp"
#include "game/plugins/lighting.hpp"
#include "game/plugins/map.hpp"
#include "game/plugins/mesh_registry.hpp"
#include "game/plugins/plugins.hpp"
#include "game/plugins/render.hpp"
#include "game/plugins/ui.hpp"
#include "game/plugins/default.hpp"
#include "lib/core/core.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

int main()
{
	try
	{
		Core app;
		app.addPlugin(std::make_unique<MeshRegistryPlugin>());
		app.addPlugin(std::make_unique<CameraPlugin>());
		app.addPlugin(std::make_unique<LightingPlugin>());
		app.addPlugin(std::make_unique<RenderPlugin>());
		app.addPlugin(std::make_unique<TransformPlugin>());
		app.addPlugin(std::make_unique<DefaultPlugin>());
		app.addPlugin(std::make_unique<UiPlugin>());
		app.init();

		entt::entity mapEntity = app.reg.create();
		spawn(app.reg, mapEntity, "models/testmap.glb",
		      "textures/viking_room.ktx2");
		app.reg.emplace<Map>(mapEntity);
		app.reg.emplace<Sky>(app.reg.create());

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
