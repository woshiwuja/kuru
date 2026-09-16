#include "core.hpp"
#include "../image/image.hpp"
#include "physics/physics.hpp"
#include <SDL3/SDL_vulkan.h>
#include <cassert>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <typeinfo>

namespace KR {

Core *Core::s_instance = nullptr;

Core::Core() {
  assert(s_instance == nullptr && "only one Core may exist at a time");
  s_instance = this;
  // Jolt's allocation hooks are null until this runs; physicsManager below
  // constructs a TempAllocatorImpl that allocates immediately.
  std::cerr << "[debug] Core::Core: RegisterDefaultAllocator\n" << std::flush;
  JPH::RegisterDefaultAllocator();
  std::cerr << "[debug] Core::Core: making Window\n" << std::flush;
  window = std::make_unique<Window>(Window{.width = 800, .height = 600});
  std::cerr << "[debug] Core::Core: making EventManager\n" << std::flush;
  eventManager = std::make_unique<EventManager>();
  std::cerr << "[debug] Core::Core: making Device\n" << std::flush;
  device = std::make_unique<Device>();
  std::cerr << "[debug] Core::Core: making Graphics\n" << std::flush;
  graphics = std::make_unique<Graphics>();
  std::cerr << "[debug] Core::Core: making Sync\n" << std::flush;
  sync = std::make_unique<Sync>();
  std::cerr << "[debug] Core::Core: making PhysicsManager\n" << std::flush;
  physicsManager = std::make_unique<PhysicsManager>();
  std::cerr << "[debug] Core::Core: done\n" << std::flush;
}

Core::~Core() {
  s_instance = nullptr;
  window = nullptr;
}

Core *Core::get() {
  assert(s_instance != nullptr && "Core::get() with no live Core");
  return s_instance;
}

void Core::init() {
  std::cerr << "[debug] Core::init: initVulkan\n" << std::flush;
  initVulkan();
  std::cerr << "[debug] Core::init: initPhysics\n" << std::flush;
  initPhysics();
  std::cerr << "[debug] Core::init: initECS\n" << std::flush;
  initECS();
  std::cerr << "[debug] Core::init: done\n" << std::flush;
  running = true;
}

void Core::run() { mainLoop(); }

void Core::end() {
  cleanup();
  window->quit();
}

void Core::initVulkan() {
  std::cerr << "[debug] initVulkan: window->init\n" << std::flush;
  window->init();
  std::cerr << "[debug] initVulkan: createInstance\n" << std::flush;
  createInstance();
  std::cerr << "[debug] initVulkan: graphics->createSurface\n" << std::flush;
  graphics->createSurface();
  std::cerr << "[debug] initVulkan: device->pickPhysicalDevice\n" << std::flush;
  device->pickPhysicalDevice();
  std::cerr << "[debug] initVulkan: device->createLogicalDevice\n" << std::flush;
  device->createLogicalDevice();
  std::cerr << "[debug] initVulkan: graphics->chooseMsaaSamples\n" << std::flush;
  graphics->chooseMsaaSamples();
  std::cerr << "[debug] initVulkan: graphics->createSwapChain\n" << std::flush;
  graphics->createSwapChain();
  std::cerr << "[debug] initVulkan: graphics->createImageViews\n" << std::flush;
  graphics->createImageViews();
  std::cerr << "[debug] initVulkan: device->createCommandPool\n" << std::flush;
  device->createCommandPool();
  std::cerr << "[debug] initVulkan: graphics->createColorResources\n" << std::flush;
  graphics->createColorResources();
  std::cerr << "[debug] initVulkan: graphics->createDepthResources\n" << std::flush;
  graphics->createDepthResources();
  std::cerr << "[debug] initVulkan: graphics->createTextureSampler\n" << std::flush;
  graphics->createTextureSampler();
  std::cerr << "[debug] initVulkan: createCommandBuffers\n" << std::flush;
  createCommandBuffers();
  std::cerr << "[debug] initVulkan: sync->init\n" << std::flush;
  sync->init();
  std::cerr << "[debug] initVulkan: done\n" << std::flush;
}

void Core::addPlugin(std::unique_ptr<Plugin> plugin) {
  plugins.emplace_back(std::move(plugin));
}
void Core::initPhysics() { physicsManager->init(); };

void Core::initECS() {
  reg.ctx().emplace<FrameContext>();
  for (auto &plugin : plugins) {
    std::cerr << "[debug] initECS: plugin->init " << typeid(*plugin).name()
               << "\n" << std::flush;
    plugin->init(reg);
  }
}

void Core::mainLoop() {
  while (running) {
    eventManager->pump();
    running = !eventManager->quit;
    drawFrame();
  }
  device->wait();
}

void Core::cleanup() {
  device->wait();
  reg = entt::registry{};
  plugins.clear();
  sync = nullptr;
  commandBuffers.clear();
  graphics = nullptr;
  device = nullptr;
  instance = nullptr;
  physicsManager->stop();
  physicsManager = nullptr;
}

void Core::createInstance() {
  vk::ApplicationInfo applicationInfo{.pApplicationName = window->title.c_str(),
                                      .applicationVersion =
                                          VK_MAKE_VERSION(1, 0, 0),
                                      .pEngineName = "Kuru",
                                      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                      .apiVersion = VK_API_VERSION_1_3};

  auto extensions = getRequiredInstanceExtensions();
  std::vector<const char *> layers;
  if (enableValidationLayers && checkValidationLayerSupport()) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
  }

  vk::InstanceCreateInfo createInfo{
      .pApplicationInfo = &applicationInfo,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data()};

  instance = vk::raii::Instance(context, createInfo);
  std::cout << "Instance created\n";
}

void Core::createCommandBuffers() {
  commandBuffers.clear();
  vk::CommandBufferAllocateInfo allocInfo{
      .commandPool = *device->commandPool,
      .level = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
  commandBuffers = vk::raii::CommandBuffers(device->device, allocInfo);
}

void Core::recordCommandBuffer(uint32_t imageIndex) {
  auto &commandBuffer = commandBuffers[sync->frameIndex];
  commandBuffer.begin({});

  // Everything per-entity is the plugins' job; Core only hands them the frame.
  // view and proj are left to whichever plugin owns the camera.
  auto &frame = reg.ctx().get<FrameContext>();
  frame.commandBuffer = &commandBuffer;
  frame.frameIndex = sync->frameIndex;
  frame.extent = graphics->swapChainExtent;
  // Outside the pass: this is where uploads and spawning belong.
  for (auto &plugin : plugins) {
    plugin->start(reg);
  }
  // Before starting rendering, transition the swapchain image to
  // COLOR_ATTACHMENT_OPTIMAL
  transitionImageLayout(
      commandBuffer, graphics->swapChainImages[imageIndex],
      vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
      {}, // srcAccessMask (no need to wait for previous operations)
      vk::AccessFlagBits2::eColorAttachmentWrite,         // dstAccessMask
      vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
      vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage
      vk::ImageAspectFlagBits::eColor);
  // Transition depth image to depth attachment optimal layout
  transitionImageLayout(commandBuffer, *graphics->depthImage,
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eDepthAttachmentOptimal,
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits2::eLateFragmentTests,
                        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits2::eLateFragmentTests,
                        vk::ImageAspectFlagBits::eDepth);

  const bool msaa = graphics->msaaSamples != vk::SampleCountFlagBits::e1;
  if (msaa) {
    transitionImageLayout(
        commandBuffer, *graphics->colorImage, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal, {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::ImageAspectFlagBits::eColor);
  }

  vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
  // With MSAA the pass draws into the multisample image and resolves into the
  // swapchain image on endRendering; without it, straight into the swapchain.
  vk::RenderingAttachmentInfo attachmentInfo = {
      .imageView = msaa ? *graphics->colorImageView
                        : *graphics->swapChainImageViews[imageIndex],
      .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .resolveMode = msaa ? vk::ResolveModeFlagBits::eAverage
                          : vk::ResolveModeFlagBits::eNone,
      .resolveImageView =
          msaa ? *graphics->swapChainImageViews[imageIndex] : nullptr,
      .resolveImageLayout = msaa ? vk::ImageLayout::eColorAttachmentOptimal
                                 : vk::ImageLayout::eUndefined,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eStore,
      .clearValue = clearColor};
  // Reversed-Z (see camera.cpp): 0 is the far plane, so that's the "nothing
  // drawn yet" value now, not 1.
  vk::ClearValue clearDepth = vk::ClearDepthStencilValue{0.0f, 0};
  vk::RenderingAttachmentInfo depthAttachmentInfo{
      .imageView = *graphics->depthImageView,
      .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eDontCare,
      .clearValue = clearDepth};
  vk::RenderingInfo renderingInfo = {
      .renderArea = {.offset = {0, 0}, .extent = graphics->swapChainExtent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &attachmentInfo,
      .pDepthAttachment = &depthAttachmentInfo};
  commandBuffer.beginRendering(renderingInfo);

  for (auto &plugin : plugins) {
    plugin->update(reg);
  }
  for (auto &plugin : plugins) {
    plugin->end(reg);
  }

  commandBuffer.endRendering();
  // After rendering, transition the swapchain image to PRESENT_SRC
  transitionImageLayout(
      commandBuffer, graphics->swapChainImages[imageIndex],
      vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
      vk::AccessFlagBits2::eColorAttachmentWrite,         // srcAccessMask
      {},                                                 // dstAccessMask
      vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
      vk::PipelineStageFlagBits2::eBottomOfPipe,          // dstStage
      vk::ImageAspectFlagBits::eColor);
  commandBuffer.end();
}

void Core::drawFrame() {
  static auto lastFrameTime = std::chrono::high_resolution_clock::now();
  const auto currentTime = std::chrono::high_resolution_clock::now();
  deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
  lastFrameTime = currentTime;

  auto fenceResult = device->device.waitForFences(
      *sync->inFlightFences[sync->frameIndex], vk::True, UINT64_MAX);
  if (fenceResult != vk::Result::eSuccess) {
    throw std::runtime_error("failed to wait for fence!");
  }

  auto [result, imageIndex] = graphics->swapChain.acquireNextImage(
      UINT64_MAX, *sync->presentCompleteSemaphores[sync->frameIndex], nullptr);

  if (result == vk::Result::eErrorOutOfDateKHR) {
    graphics->recreateSwapChain();
    return;
  }
  // On other success codes than eSuccess and eSuboptimalKHR we just throw an
  // exception. On any error code, aquireNextImage already threw an exception.
  if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
    assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  device->device.resetFences(*sync->inFlightFences[sync->frameIndex]);
  commandBuffers[sync->frameIndex].reset();
  recordCommandBuffer(imageIndex);

  vk::PipelineStageFlags waitDestinationStageMask(
      vk::PipelineStageFlagBits::eColorAttachmentOutput);
  const vk::SubmitInfo submitInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*sync->presentCompleteSemaphores[sync->frameIndex],
      .pWaitDstStageMask = &waitDestinationStageMask,
      .commandBufferCount = 1,
      .pCommandBuffers = &*commandBuffers[sync->frameIndex],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &*sync->renderFinishedSemaphores[imageIndex]};
  device->queue.submit(submitInfo, *sync->inFlightFences[sync->frameIndex]);

  const vk::PresentInfoKHR presentInfoKHR{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*sync->renderFinishedSemaphores[imageIndex],
      .swapchainCount = 1,
      .pSwapchains = &*graphics->swapChain,
      .pImageIndices = &imageIndex};
  result = device->queue.presentKHR(presentInfoKHR);
  // Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined,
  // eErrorOutOfDateKHR can be checked as a result here and does not need to
  // be caught by an exception.
  if ((result == vk::Result::eSuboptimalKHR) ||
      (result == vk::Result::eErrorOutOfDateKHR) ||
      graphics->framebufferResized) {
    graphics->framebufferResized = false;
    graphics->recreateSwapChain();
  } else {
    // There are no other success codes than eSuccess; on any error code,
    // presentKHR already threw an exception.
    assert(result == vk::Result::eSuccess);
  }
  sync->frameIndex = (sync->frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

std::vector<const char *> Core::getRequiredInstanceExtensions() const {
  uint32_t sdlExtensionCount = 0;
  const char *const *sdlExtensions =
      SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
  if (sdlExtensions == nullptr) {
    throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions: ") +
                             SDL_GetError());
  }
  std::vector<const char *> extensions(sdlExtensions,
                                       sdlExtensions + sdlExtensionCount);
  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return extensions;
}

bool Core::checkValidationLayerSupport() const {
  return (std::ranges::any_of(context.enumerateInstanceLayerProperties(),
                              [](vk::LayerProperties const &lp) {
                                return (strcmp("VK_LAYER_KHRONOS_validation",
                                               lp.layerName) == 0);
                              }));
}
} // namespace KR
