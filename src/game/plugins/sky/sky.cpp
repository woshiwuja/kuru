#include "sky.hpp"
#include <Kuru.h>
#include <random>

using namespace KR;

namespace {
vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) {
  vk::ShaderModuleCreateInfo createInfo{
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t *>(code.data())};
  return vk::raii::ShaderModule{Core::get()->device->device, createInfo};
}
} // namespace

void SkyPlugin::init(entt::registry &reg) {
  createSkyDescriptorSetLayout();
  createSkyPipeline();
  createSkyResources();

reg.emplace<Sky>(reg.create());
}

void SkyPlugin::update(entt::registry &reg) {
  for (auto [entity, sky] : reg.view<Sky>().each()) {
    ImGui::Begin("Sky");
    ImGui::ColorEdit3("tint", &sky.color.x);
    ImGui::ColorEdit3("sun", &sky.sunColor.x);
    ImGui::End();
  }
  updateSkyUniforms(reg);
  drawSky(reg);
}

void SkyPlugin::createSkyDescriptorSetLayout() {
  auto &device = Core::get()->device;
  // Matches the explicit [[vk::binding(...)]] indices in sky_clouds.slang:
  // 0 = ShaderConstants, 1 = iChannel0 (Texture2D), 2 = iChannel0Sampler.
  std::array bindings = {
      vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eFragment,
                                     nullptr),
      vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eSampledImage, 1,
                                     vk::ShaderStageFlagBits::eFragment,
                                     nullptr),
      vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eSampler, 1,
                                     vk::ShaderStageFlagBits::eFragment,
                                     nullptr)};

  vk::DescriptorSetLayoutCreateInfo layoutInfo{
      .bindingCount = static_cast<uint32_t>(bindings.size()),
      .pBindings = bindings.data()};
  skyDescriptorSetLayout =
      vk::raii::DescriptorSetLayout(device->device, layoutInfo);
}

void SkyPlugin::createSkyPipeline() {
  auto core = Core::get();
  vk::raii::ShaderModule shaderModule =
      createShaderModule(readFile("shaders/sky_clouds.spv"));

  vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
      .stage = vk::ShaderStageFlagBits::eVertex,
      .module = *shaderModule,
      .pName = "vertMain"};
  vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
      .stage = vk::ShaderStageFlagBits::eFragment,
      .module = *shaderModule,
      .pName = "fragmentMain"};
  vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                      fragShaderStageInfo};

  // No vertex buffer bound for this draw: vertMain synthesizes a fullscreen
  // triangle from SV_VertexID alone.
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      .topology = vk::PrimitiveTopology::eTriangleList,
      .primitiveRestartEnable = vk::False};
  vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                    .scissorCount = 1};
  vk::PipelineRasterizationStateCreateInfo rasterizer{
      .depthClampEnable = vk::False,
      .rasterizerDiscardEnable = vk::False,
      .polygonMode = vk::PolygonMode::eFill,
      // The synthesized triangle's winding isn't worth pinning down; just draw
      // both ways.
      .cullMode = vk::CullModeFlagBits::eNone,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .depthBiasEnable = vk::False,
      .lineWidth = 1.0f};
  vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = core->graphics->msaaSamples,
      .sampleShadingEnable = vk::False};
  // Off both ways: the sky must never occlude or be occluded by real geometry,
  // it only ever fills in pixels nothing else drew.
  vk::PipelineDepthStencilStateCreateInfo depthStencil{
      .depthTestEnable = vk::False,
      .depthWriteEnable = vk::False,
      .depthCompareOp = vk::CompareOp::eLess,
      .depthBoundsTestEnable = vk::False,
      .stencilTestEnable = vk::False};
  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
  vk::PipelineColorBlendStateCreateInfo colorBlending{
      .logicOpEnable = vk::False,
      .logicOp = vk::LogicOp::eCopy,
      .attachmentCount = 1,
      .pAttachments = &colorBlendAttachment};
  std::vector dynamicStates = {vk::DynamicState::eViewport,
                               vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState{
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data()};

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{.setLayoutCount = 1,
                                                  .pSetLayouts =
                                                      &*skyDescriptorSetLayout,
                                                  .pushConstantRangeCount = 0};
  skyPipelineLayout =
      vk::raii::PipelineLayout(core->device->device, pipelineLayoutInfo);

  vk::Format depthFormat = core->graphics->findDepthFormat();

  // Dynamic rendering requires attachment formats to match the pass this
  // pipeline is used in, even though this one never touches the depth image.
  vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                     vk::PipelineRenderingCreateInfo>
      pipelineCreateInfoChain = {
          {.stageCount = 2,
           .pStages = shaderStages,
           .pVertexInputState = &vertexInputInfo,
           .pInputAssemblyState = &inputAssembly,
           .pViewportState = &viewportState,
           .pRasterizationState = &rasterizer,
           .pMultisampleState = &multisampling,
           .pDepthStencilState = &depthStencil,
           .pColorBlendState = &colorBlending,
           .pDynamicState = &dynamicState,
           .layout = *skyPipelineLayout,
           .renderPass = nullptr},
          {.colorAttachmentCount = 1,
           .pColorAttachmentFormats =
               &core->graphics->swapChainSurfaceFormat.format,
           .depthAttachmentFormat = depthFormat}};

  skyPipeline = vk::raii::Pipeline(
      core->device->device, nullptr,
      pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

namespace {
// sky_clouds.slang wants a Shadertoy-style RGBA noise texture (iChannel0) and
// there's no asset for one, so this stands in for it. Fixed seed: reproducible
// runs, not cryptographic.
std::vector<unsigned char> generateNoisePixels(uint32_t size) {
  std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
  std::mt19937 rng(1337);
  std::uniform_int_distribution<int> byteDist(0, 255);
  for (auto &channel : pixels) {
    channel = static_cast<unsigned char>(byteDist(rng));
  }
  return pixels;
}
} // namespace

void SkyPlugin::createSkyResources() {
  auto core = Core::get();

  constexpr uint32_t noiseSize =
      256; // matches the /256.0 tiling in sky_clouds.slang
  std::vector<unsigned char> noise = generateNoisePixels(noiseSize);
  // Unorm, not Srgb: these are data values, not color, and must not be
  // gamma-decoded on sample.
  skyNoiseTexture = loadTextureFromPixels(noise.data(), noiseSize, noiseSize,
                                          vk::Format::eR8G8B8A8Unorm);

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DeviceSize bufferSize = sizeof(SkyUniformBufferObject);
    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory bufferMemory = nullptr;
    createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 buffer, bufferMemory);
    skyUniformBuffers.emplace_back(std::move(buffer));
    skyUniformBuffersMemory.emplace_back(std::move(bufferMemory));
    skyUniformBuffersMapped.emplace_back(
        skyUniformBuffersMemory[i].mapMemory(0, bufferSize));
  }

  // Sized for one set per frame in flight: the sky is a single global draw,
  // not one-per-entity like the mesh descriptor pool.
  std::array poolSize{vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,
                                             MAX_FRAMES_IN_FLIGHT),
                      vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage,
                                             MAX_FRAMES_IN_FLIGHT),
                      vk::DescriptorPoolSize(vk::DescriptorType::eSampler,
                                             MAX_FRAMES_IN_FLIGHT)};
  vk::DescriptorPoolCreateInfo poolInfo{
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = MAX_FRAMES_IN_FLIGHT,
      .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
      .pPoolSizes = poolSize.data()};
  skyDescriptorPool = vk::raii::DescriptorPool(core->device->device, poolInfo);

  std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                               *skyDescriptorSetLayout);
  vk::DescriptorSetAllocateInfo allocInfo{
      .descriptorPool = *skyDescriptorPool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data()};
  skyDescriptorSets = core->device->device.allocateDescriptorSets(allocInfo);

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DescriptorBufferInfo bufferInfo{.buffer = *skyUniformBuffers[i],
                                        .offset = 0,
                                        .range =
                                            sizeof(SkyUniformBufferObject)};
    vk::DescriptorImageInfo imageInfo{
        .sampler = nullptr,
        .imageView = *skyNoiseTexture->view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo samplerInfo{.sampler = *core->graphics->sampler,
                                        .imageView = nullptr,
                                        .imageLayout =
                                            vk::ImageLayout::eUndefined};
    std::array descriptorWrites{
        vk::WriteDescriptorSet{.dstSet = *skyDescriptorSets[i],
                               .dstBinding = 0,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eUniformBuffer,
                               .pBufferInfo = &bufferInfo},
        vk::WriteDescriptorSet{.dstSet = *skyDescriptorSets[i],
                               .dstBinding = 1,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eSampledImage,
                               .pImageInfo = &imageInfo},
        vk::WriteDescriptorSet{.dstSet = *skyDescriptorSets[i],
                               .dstBinding = 2,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType = vk::DescriptorType::eSampler,
                               .pImageInfo = &samplerInfo}};
    core->device->device.updateDescriptorSets(descriptorWrites, {});
  }
}

void SkyPlugin::updateSkyUniforms(entt::registry &reg) {
  auto skyView = reg.view<Sky>();
  if (skyView.begin() == skyView.end()) {
    return;
  }
  const Sky &sky = skyView.get<Sky>(*skyView.begin());
  const auto &frame = reg.ctx().get<FrameContext>();
  skyTime += Core::get()->deltaTime() * .1f;
  // Rotation only: dropping the view matrix's translation is what keeps the
  // sky from shifting as the camera moves, while still rotating with it.
  glm::mat4 viewRotOnly = glm::mat4(glm::mat3(frame.view));

  SkyUniformBufferObject ubo{
      .resolution = {static_cast<float>(frame.extent.width),
                     static_cast<float>(frame.extent.height)},
      .time = skyTime,
      .invViewRotProj = glm::inverse(frame.skyRayProj * viewRotOnly),
      .sunColor = sky.sunColor,
      .skyTint = sky.color};
  memcpy(skyUniformBuffersMapped[frame.frameIndex], &ubo, sizeof(ubo));
}

void SkyPlugin::drawSky(entt::registry &reg) {
  auto skyView = reg.view<Sky>();
  if (skyView.begin() == skyView.end()) {
    return;
  }

  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *skyPipeline);
  commandBuffer.setViewport(
      0, vk::Viewport(0.0f, 0.0f, static_cast<float>(frame.extent.width),
                      static_cast<float>(frame.extent.height), 0.0f, 1.0f));
  commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), frame.extent));
  commandBuffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, *skyPipelineLayout, 0,
      *skyDescriptorSets[frame.frameIndex], nullptr);
  commandBuffer.draw(
      3, 1, 0, 0); // no vertex/index buffer: vertMain synthesizes the triangle
}
