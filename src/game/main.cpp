#include <Kuru.h>
#include "plugins/camera/camera.hpp"
#include "plugins/character/character.hpp"
#include "plugins/character/controller.hpp"
#include "plugins/inspector/inspector.hpp"
#include "plugins/inventory/inventory.hpp"
#include "plugins/item/item.hpp"
#include "plugins/map/map.hpp"
#include "plugins/navigation/navigation.hpp"
#include "plugins/outline/outline.hpp"
#include "plugins/parallax/parallax.hpp"
#include "plugins/name/name.hpp"
#include "plugins/sky/sky.hpp"
#include "plugins/hair/hair.hpp"
#include "plugins/dialog/dialog.hpp"
#include "plugins/limb/limb.hpp"
#include "plugins/picking/picking.hpp"
#include "plugins/default.hpp"
#include "plugins/pausing/pausing.hpp"
#include "plugins/settings/settings.hpp"
#include "plugins/save/save.hpp"
#include "plugins/terminal/terminal.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace KR;

int main() {
  try {
    Core app;
    std::vector<std::unique_ptr<Plugin>> plugins;
    plugins.push_back(std::make_unique<CameraPlugin>());
    // Sky draws in update(), so it has to come before RenderPlugin.
    plugins.push_back(std::make_unique<SkyPlugin>());
    for (auto &plugin : defaultPlugins()) {
      plugins.push_back(std::move(plugin));
    }
    plugins.push_back(std::make_unique<ParallaxPlugin>());
    plugins.push_back(std::make_unique<OutlinePlugin>());
    plugins.push_back(std::make_unique<DefaultPlugin>());
    plugins.push_back(std::make_unique<MapPlugin>());
    plugins.push_back(std::make_unique<NavigationPlugin>());
    plugins.push_back(std::make_unique<CharacterPlugin>());
    plugins.push_back(std::make_unique<CharacterControllerPlugin>());
    plugins.push_back(std::make_unique<LimbPlugin>());
    plugins.push_back(std::make_unique<InspectorPlugin>());
    plugins.push_back(std::make_unique<StatPlugin>());
    plugins.push_back(std::make_unique<InventoryPlugin>());
    plugins.push_back(std::make_unique<DialoguePlugin>());
    // plugins.push_back(std::make_unique<TerminalPlugin>());
    plugins.push_back(std::make_unique<NamePlugin>());
    plugins.push_back(std::make_unique<ItemPlugin>());
    // plugins.push_back(std::make_unique<HairPlugin>());
    plugins.push_back(std::make_unique<PickingPlugin>());
    plugins.push_back(std::make_unique<SavePlugin>());
    plugins.push_back(std::make_unique<SettingsPlugin>());
    plugins.push_back(std::make_unique<PausingPlugin>());

    app.addPlugins(std::move(plugins));

    app.init();
    app.run();
    app.end();
  } catch (const std::exception &e) {
    fprintf(stderr, "%s\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
