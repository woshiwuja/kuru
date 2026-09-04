#include "core.hpp"
#include "../image/image.hpp"
#include <SDL3/SDL_vulkan.h>
#include <cassert>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

Core::Core() {
  assert(s_instance == nullptr && "only one Core may exist at a time");
  s_instance = this;
  window = std::make_unique<Window>(Window{.width = 800, .height = 600});
  eventManager = std::make_unique<EventManager>();
  device = std::make_unique<Device>();
  graphics = std::make_unique<Graphics>();
  sync = std::make_unique<Sync>();
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
  initVulkan();
  initECS();
  running = true;
}

void Core::run() { mainLoop(); }

void Core::end() {
  cleanup();
  // SDL_Quit unloads the Vulkan loader, so every Vulkan object must already be
  // gone: destroying one afterwards jumps through dangling function pointers.
  window->quit();
}

void Core::initVulkan() {
  window->init();
  createInstance();
  graphics->createSurface();
  device->pickPhysicalDevice();
  device->createLogicalDevice();
  graphics->createSwapChain();
  graphics->createImageViews();
  device->createCommandPool();
  graphics->createDepthResources();
  graphics->createTextureSampler();
  createCommandBuffers();
  sync->init();
}

void Core::addPlugin(std::unique_ptr<Plugin> plugin) {
  plugins.emplace_back(std::move(plugin));
}

void Core::initECS() {
  // The frame contract is the engine's, not any one plugin's.
  reg.ctx().emplace<FrameContext>();
  for (auto &plugin : plugins) {
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
  // Destroy the whole registry, not just the entities: the mesh and texture
  // caches live in reg.ctx(), which reg.clear() leaves alone, and they hold
  // buffers and images that must go while the device is still around.
  reg = entt::registry{};
  plugins.clear();
  sync = nullptr;
  commandBuffers.clear();
  graphics = nullptr;
  device = nullptr;
  instance = nullptr;
}

void Core::createInstance() {
  vk::ApplicationInfo applicationInfo{
      .pApplicationName = window->title.c_str(),
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
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

  vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
  vk::RenderingAttachmentInfo attachmentInfo = {
      .imageView = *graphics->swapChainImageViews[imageIndex],
      .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eStore,
      .clearValue = clearColor};
  vk::ClearValue clearDepth = vk::ClearDepthStencilValue{1.0f, 0};
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

  // Everything per-entity is the plugins' job; Core only hands them the frame.
  auto &frame = reg.ctx().get<FrameContext>();
  frame.commandBuffer = &commandBuffer;
  frame.frameIndex = sync->frameIndex;
  frame.extent = graphics->swapChainExtent;
  // view and proj are left to whichever plugin owns the camera.
  // ponytail: every plugin runs inside the render pass; split into phases when
  // one that does not draw shows up.
  for (auto &plugin : plugins) {
    plugin->run(reg);
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
