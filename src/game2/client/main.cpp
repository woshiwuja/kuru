#include <Kuru.h>
#include "plugin/plugin.hpp"
#include "../plugins/camera/camera.hpp"
#include "../plugins/character/character.hpp"
#include "../plugins/network/client.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

using namespace KR;

int main() {
  try {
    Core app;
    std::vector<std::unique_ptr<Plugin>> plugins;
    plugins.push_back(std::make_unique<CharacterPlugin>());
    plugins.push_back(std::make_unique<CameraPlugin>());
    plugins.push_back(std::make_unique<NetClientPlugin>(
        glm::vec4(0.8f, 0.2f, 0.2f, 1.0f)));
    auto default_plugins = KR::defaultPlugins();
    for (auto &p : default_plugins) {
      plugins.push_back(std::move(p));
    }
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
