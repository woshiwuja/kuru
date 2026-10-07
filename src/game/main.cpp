#include <Kuru.h>
#include "plugins/camera/camera.hpp"
#include "plugins/character/character.hpp"
#include "plugins/character/controller.hpp"
#include "plugins/inspector/inspector.hpp"
#include "plugins/inventory/inventory.hpp"
#include "plugins/lighting/lighting.hpp"
#include "plugins/map/map.hpp"
#include "plugins/render/mesh_registry.hpp"
#include "plugins/navigation/navigation.hpp"
#include "plugins/outline/outline.hpp"
#include "plugins/physics/physics.hpp"
#include "plugins/render/render.hpp"
#include "plugins/sky/sky.hpp"
#include "plugins/ui/ui.hpp"
#include "plugins/material/material.hpp"
#include "plugins/model/model.hpp"
#include "plugins/picking/picking.hpp"
#include "plugins/default.hpp"
#include "plugins/pausing/pausing.hpp"
#include "plugins/settings/settings.hpp"
#include "plugins/save/save.hpp"
#include "plugins/physics/physics.hpp"
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
		app.addPlugin(std::make_unique<SkyPlugin>()); // before RenderPlugin: meshes draw over the sky
		app.addPlugin(std::make_unique<RenderPlugin>());
		app.addPlugin(std::make_unique<OutlinePlugin>()); // after RenderPlugin: borrows its layout, draws over its meshes
		app.addPlugin(std::make_unique<TransformPlugin>());
		app.addPlugin(std::make_unique<DefaultPlugin>());
		app.addPlugin(std::make_unique<UIPlugin>());
		app.addPlugin(std::make_unique<MapPlugin>());
		app.addPlugin(std::make_unique<NavigationPlugin>());
		app.addPlugin(std::make_unique<CharacterPlugin>());
		app.addPlugin(std::make_unique<CharacterControllerPlugin>());
		app.addPlugin(std::make_unique<InspectorPlugin>());
		app.addPlugin(std::make_unique<StatPlugin>());
		app.addPlugin(std::make_unique<InventoryPlugin>());
		app.addPlugin(std::make_unique<NamePlugin>());
		app.addPlugin(std::make_unique<MaterialPlugin>());
		app.addPlugin(std::make_unique<TexturePlugin>());
		app.addPlugin(std::make_unique<ModelPlugin>());
		app.addPlugin(std::make_unique<PickingPlugin>()); // after TransformPlugin: see update()
		app.addPlugin(std::make_unique<SavePlugin>()); // before PausingPlugin: it reads SaveMenu
		app.addPlugin(std::make_unique<SettingsPlugin>()); // before PausingPlugin: it reads SettingsMenu
		app.addPlugin(std::make_unique<PausingPlugin>());
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
