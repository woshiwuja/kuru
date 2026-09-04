#pragma once
#include "../common/common.hpp"
#include "../device/device.hpp"
#include "../event/event.hpp"
#include "../graphics/graphics.hpp"
#include "../plugin/plugin.hpp"
#include "../sync/sync.hpp"
#include "../window/window.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <vulkan/vulkan_profiles.hpp>
#include <vulkan/vulkan_raii.hpp>

struct AppInfo {
  bool profileSupported = false;
  VpProfileProperties profile;
};

// Bootstrap and frame loop only. Anything that belongs to a subsystem lives
// with that subsystem: uploads on Device, images in image/, meshes in model/,
// the pipeline and per-entity state in the render plugin.
struct Core {
  bool running = false;
  std::unique_ptr<Window> window = nullptr;
  std::unique_ptr<EventManager> eventManager = nullptr;
  std::unique_ptr<Device> device = nullptr;
  std::unique_ptr<Graphics> graphics = nullptr;
  Core();
  ~Core();

  // The live Core. main() owns it as a local, this only borrows a pointer, so
  // calling get() before that local is constructed or after it dies is a bug.
  static Core *get();

  void init();
  void run();
  void end();

  static inline Core *s_instance =
      nullptr; // `instance` is taken by the vk::raii::Instance member
  AppInfo appInfo = {};
  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  std::vector<vk::raii::CommandBuffer> commandBuffers;
  std::unique_ptr<Sync> sync = nullptr;

  // Declared after the plugins on purpose: entities hold GPU state owned by
  // plugin pools, so the registry must be torn down first.
  std::vector<std::unique_ptr<Plugin>> plugins;
  entt::registry reg;
  // Plugins must be registered before init(): that is when they build their
  // pipelines, and the device has to exist by then.
  void addPlugin(std::unique_ptr<Plugin> plugin);

  glm::vec3 cameraPos = {2.0f, 2.0f, 2.0f};
  float cameraYaw = -135.0f;
  float cameraPitch = -30.0f;
  [[nodiscard]] glm::vec3 cameraFront() const;
  void processInput(float deltaTime);

  void initVulkan();
  void initECS();
  void mainLoop();
  void cleanup();
  void createInstance();
  void createCommandBuffers();
  void recordCommandBuffer(uint32_t imageIndex);
  void drawFrame();
  [[nodiscard]] std::vector<const char *> getRequiredInstanceExtensions() const;
  [[nodiscard]] bool checkValidationLayerSupport() const;
};
