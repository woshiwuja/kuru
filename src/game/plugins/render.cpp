#include "render.hpp"
#include "lighting.hpp"
#include "../../lib/common/common.hpp"
#include "../../lib/core/core.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <random>

void RenderPlugin::init(entt::registry &reg) {
  // Other plugins reach the renderer through the registry, not a global.
  reg.ctx().emplace<RenderPlugin *>(this);
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createDescriptorPool();
  createSkyDescriptorSetLayout();
  createSkyPipeline();
  createSkyResources();
}

void RenderPlugin::createDescriptorSetLayout() {
  auto &device = Core::get()->device;
  std::array bindings = {
      vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eVertex |
                                         vk::ShaderStageFlagBits::eFragment,
                                     nullptr),
      vk::DescriptorSetLayoutBinding(
          1, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr)};

  vk::DescriptorSetLayoutCreateInfo layoutInfo{
      .bindingCount = static_cast<uint32_t>(bindings.size()),
      .pBindings = bindings.data()};
  descriptorSetLayout =
      vk::raii::DescriptorSetLayout(device->device, layoutInfo);
}

vk::raii::ShaderModule
RenderPlugin::createShaderModule(const std::vector<char> &code) const {
  vk::ShaderModuleCreateInfo createInfo{
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t *>(code.data())};
  return vk::raii::ShaderModule{Core::get()->device->device, createInfo};
}

void RenderPlugin::createGraphicsPipeline() {
  auto core = Core::get();
  vk::raii::ShaderModule shaderModule =
      createShaderModule(readFile("shaders/slang.spv"));

  vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
      .stage = vk::ShaderStageFlagBits::eVertex,
      .module = *shaderModule,
      .pName = "vertMain"};
  vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
      .stage = vk::ShaderStageFlagBits::eFragment,
      .module = *shaderModule,
      .pName = "fragMain"};
  vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                      fragShaderStageInfo};

  auto bindingDescription = Vertex::getBindingDescription();
  auto attributeDescriptions = Vertex::getAttributeDescriptions();
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &bindingDescription,
      .vertexAttributeDescriptionCount =
          static_cast<uint32_t>(attributeDescriptions.size()),
      .pVertexAttributeDescriptions = attributeDescriptions.data()};
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      .topology = vk::PrimitiveTopology::eTriangleList,
      .primitiveRestartEnable = vk::False};
  vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                    .scissorCount = 1};
  vk::PipelineRasterizationStateCreateInfo rasterizer{
      .depthClampEnable = vk::False,
      .rasterizerDiscardEnable = vk::False,
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eBack,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .depthBiasEnable = vk::False,
      .lineWidth = 1.0f};
  vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = vk::SampleCountFlagBits::e1,
      .sampleShadingEnable = vk::False};
  vk::PipelineDepthStencilStateCreateInfo depthStencil{
      .depthTestEnable = vk::True,
      .depthWriteEnable = vk::True,
      // Reversed-Z (see camera.cpp): near is depth 1, far is depth 0, so
      // "closer" now means "greater".
      .depthCompareOp = vk::CompareOp::eGreater,
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
                                                      &*descriptorSetLayout,
                                                  .pushConstantRangeCount = 0};

  pipelineLayout =
      vk::raii::PipelineLayout(core->device->device, pipelineLayoutInfo);

  vk::Format depthFormat = core->graphics->findDepthFormat();

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
           .layout = *pipelineLayout,
           .renderPass = nullptr},
          {.colorAttachmentCount = 1,
           .pColorAttachmentFormats =
               &core->graphics->swapChainSurfaceFormat.format,
           .depthAttachmentFormat = depthFormat}};

  graphicsPipeline = vk::raii::Pipeline(
      core->device->device, nullptr,
      pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void RenderPlugin::createDescriptorPool() {
  // We need MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT descriptor sets
  std::array poolSize{
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,
                             MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT),
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,
                             MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT)};
  vk::DescriptorPoolCreateInfo poolInfo{
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT,
      .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
      .pPoolSizes = poolSize.data()};
  descriptorPool =
      vk::raii::DescriptorPool(Core::get()->device->device, poolInfo);
}

void RenderPlugin::createSkyDescriptorSetLayout() {
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

void RenderPlugin::createSkyPipeline() {
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
      .rasterizationSamples = vk::SampleCountFlagBits::e1,
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

void RenderPlugin::createSkyResources() {
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

void RenderPlugin::update(entt::registry &reg) {
  updateUniforms(reg);
  updateSkyUniforms(reg);
  drawSky(reg); // first: meshes should draw over it, not the other way round
  drawMeshes(reg);
}

void RenderPlugin::spawn(entt::registry &reg, entt::entity entity,
                         std::shared_ptr<Mesh> mesh,
                         std::shared_ptr<Texture> texture,
                         glm::vec4 params, Transform transform) {
  assert(mesh);
  if (!reg.all_of<Transform>(entity)) {
    reg.emplace<Transform>(entity, transform);
  }
  reg.emplace<MeshRef>(entity, std::move(mesh));
  attach(reg, entity, std::move(texture), params);
}

// Requires MeshRef to already be on `entity` - RenderPlugin::spawn emplaces
// it right before calling this.
void RenderPlugin::attach(entt::registry &reg, entt::entity entity,
                          std::shared_ptr<Texture> texture, glm::vec4 params) {
  assert(texture);
  auto core = Core::get();

  Renderable renderable;
  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory bufferMemory = nullptr;
    createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 buffer, bufferMemory);
    renderable.uniformBuffers.emplace_back(std::move(buffer));
    renderable.uniformBuffersMemory.emplace_back(std::move(bufferMemory));
    renderable.uniformBuffersMapped.emplace_back(
        renderable.uniformBuffersMemory[i].mapMemory(0, bufferSize));
  }

  // One descriptor set per submesh (same uniform buffers throughout, only the
  // bound texture changes), so a multi-material mesh draws each part with its
  // own texture instead of stretching one texture over the whole thing.
  const Mesh &mesh = *reg.get<MeshRef>(entity).mesh;
  const size_t subCount = std::max<size_t>(1, mesh.submeshes.size());
  renderable.descriptorSets.resize(subCount);

  for (size_t s = 0; s < subCount; s++) {
    const std::shared_ptr<Texture> &subTexture =
        (s < mesh.submeshes.size() && mesh.submeshes[s].texture)
            ? mesh.submeshes[s].texture
            : texture;

    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                                 *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = *descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()};
    renderable.descriptorSets[s] =
        core->device->device.allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DescriptorBufferInfo bufferInfo{.buffer = *renderable.uniformBuffers[i],
                                          .offset = 0,
                                          .range = sizeof(UniformBufferObject)};
      vk::DescriptorImageInfo imageInfo{
          .sampler = *core->graphics->sampler,
          .imageView = *subTexture->view,
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      std::array descriptorWrites{
          vk::WriteDescriptorSet{.dstSet = *renderable.descriptorSets[s][i],
                                 .dstBinding = 0,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eUniformBuffer,
                                 .pBufferInfo = &bufferInfo},
          vk::WriteDescriptorSet{.dstSet = *renderable.descriptorSets[s][i],
                                 .dstBinding = 1,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &imageInfo}};
      core->device->device.updateDescriptorSets(descriptorWrites, {});
    }
  }

  if (!reg.all_of<Transform>(entity)) {
    reg.emplace<Transform>(entity);
  }
  reg.emplace<MaterialRef>(entity, std::move(texture), params);
  reg.emplace<Renderable>(entity, std::move(renderable));
}

void RenderPlugin::despawn(entt::registry &reg, entt::entity entity) {
  // Unloading is an asset swap, not a per-frame op, so a full idle is the cheap
  // correct way to know the GPU is done with these buffers.
  // ponytail: device idle per despawn; per-frame trash buckets if it ever
  // hitches
  Core::get()->device->wait();
  reg.destroy(entity);
}

void RenderPlugin::updateUniforms(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();

  // ponytail: first light only, same as CameraPlugin's one-camera assumption.
  glm::vec4 sunDirection = {0.0f, 1.0f, 0.0f, 0.0f};
  glm::vec4 sunColor = {1.0f, 1.0f, 1.0f, 0.0f};
  auto lightView = reg.view<DirectionalLight>();
  if (lightView.begin() != lightView.end()) {
    const DirectionalLight &light = lightView.get<DirectionalLight>(*lightView.begin());
    sunDirection = glm::vec4(glm::normalize(light.direction), 0.0f);
    sunColor = glm::vec4(light.color, 0.0f);
  }

  for (auto [entity, transform, material, renderable] :
       reg.view<Transform, MaterialRef, Renderable>().each()) {
    UniformBufferObject ubo{.model = transform.matrix(),
                            .view = frame.view,
                            .proj = frame.proj,
                            .material = material.params,
                            .sunDirection = sunDirection,
                            .sunColor = sunColor};
    memcpy(renderable.uniformBuffersMapped[frame.frameIndex], &ubo,
           sizeof(ubo));
  }
}

void RenderPlugin::drawMeshes(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                             *graphicsPipeline);
  commandBuffer.setViewport(
      0, vk::Viewport(0.0f, 0.0f, static_cast<float>(frame.extent.width),
                      static_cast<float>(frame.extent.height), 0.0f, 1.0f));
  commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), frame.extent));

  // ponytail: storage order, one bind pair per submesh. Group by MaterialRef
  // (reg.group<MeshRef, MaterialRef>()) when the bind count starts to show.
  for (auto [entity, meshRef, renderable] :
       reg.view<MeshRef, Renderable>().each()) {
    const Mesh &mesh = *meshRef.mesh;
    commandBuffer.bindVertexBuffers(0, *mesh.vertexBuffer, {0});
    commandBuffer.bindIndexBuffer(*mesh.indexBuffer, 0, vk::IndexType::eUint32);

    if (mesh.submeshes.empty()) {
      // Defensive only: loadModel always fills submeshes in for every mesh it
      // produces, so this only matters for a Mesh built some other way.
      commandBuffer.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0,
          *renderable.descriptorSets[0][frame.frameIndex], nullptr);
      commandBuffer.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
      continue;
    }

    for (size_t s = 0; s < mesh.submeshes.size(); s++) {
      const SubMesh &sub = mesh.submeshes[s];
      commandBuffer.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0,
          *renderable.descriptorSets[s][frame.frameIndex], nullptr);
      commandBuffer.drawIndexed(sub.indexCount, 1, sub.indexOffset, 0, 0);
    }
  }
}

void RenderPlugin::updateSkyUniforms(entt::registry &reg) {
  auto skyView = reg.view<Sky>();
  if (skyView.begin() == skyView.end()) {
    return;
  }
  const auto &frame = reg.ctx().get<FrameContext>();
  skyTime += Core::get()->deltaTime*.1f;
  // Rotation only: dropping the view matrix's translation is what keeps the
  // sky from shifting as the camera moves, while still rotating with it.
  glm::mat4 viewRotOnly = glm::mat4(glm::mat3(frame.view));

  // ponytail: first light only, same as updateUniforms.
  glm::vec4 sunColor = {1.0f, 1.0f, 1.0f, 0.0f};
  auto lightView = reg.view<DirectionalLight>();
  if (lightView.begin() != lightView.end()) {
    sunColor = glm::vec4(
        lightView.get<DirectionalLight>(*lightView.begin()).color, 0.0f);
  }

  SkyUniformBufferObject ubo{
      .resolution = {static_cast<float>(frame.extent.width),
                     static_cast<float>(frame.extent.height)},
      .time = skyTime,
      .invViewRotProj = glm::inverse(frame.skyRayProj * viewRotOnly),
      .sunColor = sunColor};
  memcpy(skyUniformBuffersMapped[frame.frameIndex], &ubo, sizeof(ubo));
}

void RenderPlugin::drawSky(entt::registry &reg) {
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
