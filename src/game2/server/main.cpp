#include <Kuru.h>
#include "../plugins/network/network.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace KR;

// Dedicated server: a headless Core, no window or rendering.
// Usage: Kuru2Server [port]
int main(int argc, char **argv) {
  try {
    uint16_t port = NET_PORT;
    if (argc > 1) {
      port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    Core app;
    app.headless = true;
    app.addPlugin(std::make_unique<NetServerPlugin>(port));
    app.init();
    printf("listening on UDP port %u\n", port);
    app.run();
    app.end();
  } catch (const std::exception &e) {
    fprintf(stderr, "%s\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
