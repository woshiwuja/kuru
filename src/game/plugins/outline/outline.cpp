#include "outline.hpp"
#include "../plugins.hpp"

using namespace KR;

void OutlinePlugin::init(entt::registry &reg) {
  // The prepass borrows the mesh pipeline layout and per-entity descriptor
  // sets, so RenderPlugin has to be registered (and initialised) before this.
  render = &renderer(reg);
  createNormalPipeline();
  createOutlineDescriptorSetLayout();
  createOutlinePipeline();
  createOutlineDescriptorSet();
}

// The normal/distance prepass renders into its own target, so it needs its own
// pass - and a pass can't be opened inside the one Core has already begun.
// start() is the only hook that runs outside it.
void OutlinePlugin::start(entt::registry &reg) { drawNormalPrepass(reg); }

// Last in the pass: it darkens whatever the sky and the meshes left behind,
// which registration order after RenderPlugin gives us.
void OutlinePlugin::update(entt::registry &reg) { drawOutline(reg); }

// ---- outline ----------------------------------------------------------------
// assets/shaders/outline.slang, in two halves: a prepass that writes world
// normal + view distance into graphics->normalImage, and a fullscreen triangle
// that samples it and subtracts the discontinuities it finds from the frame.

void OutlinePlugin::createNormalPipeline() {
  auto core = Core::get();
  vk::raii::ShaderModule shaderModule =
      render->createShaderModule(readFile("shaders/outline.spv"));

  vk::PipelineShaderStageCreateInfo shaderStages[] = {
      {.stage = vk::ShaderStageFlagBits::eVertex,
       .module = *shaderModule,
       .pName = "prepassVert"},
      {.stage = vk::ShaderStageFlagBits::eFragment,
       .module = *shaderModule,
       .pName = "prepassFrag"}};

  auto bindingDescription = Vertex::getBindingDescription();
  auto attributeDescriptions = Vertex::getAttributeDescriptions();
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &bindingDescription,
      .vertexAttributeDescriptionCount =
          static_cast<uint32_t>(attributeDescriptions.size()),
      .pVertexAttributeDescriptions = attributeDescriptions.data()};
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      .topology = vk::PrimitiveTopology::eTriangleList};
  vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                    .scissorCount = 1};
  vk::PipelineRasterizationStateCreateInfo rasterizer{
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eBack,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .lineWidth = 1.0f};
  // 1x, matching the target: see Graphics::createNormalResources.
  vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = vk::SampleCountFlagBits::e1};
  // Same reversed-Z convention as the main pass.
  vk::PipelineDepthStencilStateCreateInfo depthStencil{
      .depthTestEnable = vk::True,
      .depthWriteEnable = vk::True,
      .depthCompareOp = vk::CompareOp::eGreater};
  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
  vk::PipelineColorBlendStateCreateInfo colorBlending{
      .attachmentCount = 1, .pAttachments = &colorBlendAttachment};
  std::vector dynamicStates = {vk::DynamicState::eViewport,
                               vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState{
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data()};

  vk::Format depthFormat = core->graphics->findDepthFormat();
  vk::Format colorFormat = Graphics::normalFormat;

  // No layout of its own: prepassVert/prepassFrag read the same UBO at binding
  // 0 that the mesh shader does, so the mesh layout and its per-entity
  // descriptor sets are bound unchanged.
  vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                     vk::PipelineRenderingCreateInfo>
      chain = {{.stageCount = 2,
                .pStages = shaderStages,
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *render->pipelineLayout,
                .renderPass = nullptr},
               {.colorAttachmentCount = 1,
                .pColorAttachmentFormats = &colorFormat,
                .depthAttachmentFormat = depthFormat}};

  normalPipeline =
      vk::raii::Pipeline(core->device->device, nullptr,
                         chain.get<vk::GraphicsPipelineCreateInfo>());
}

void OutlinePlugin::createOutlineDescriptorSetLayout() {
  // Matches the explicit [[vk::binding(...)]] indices in outline.slang:
  // 1 = gbuffer (Texture2D), 2 = gbufferSampler. Binding 0 there is the
  // prepass's UBO, which this half of the file never reads.
  std::array bindings = {
      vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eSampledImage, 1,
                                     vk::ShaderStageFlagBits::eFragment,
                                     nullptr),
      vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eSampler, 1,
                                     vk::ShaderStageFlagBits::eFragment,
                                     nullptr)};
  vk::DescriptorSetLayoutCreateInfo layoutInfo{
      .bindingCount = static_cast<uint32_t>(bindings.size()),
      .pBindings = bindings.data()};
  outlineDescriptorSetLayout = vk::raii::DescriptorSetLayout(
      Core::get()->device->device, layoutInfo);
}

void OutlinePlugin::createOutlinePipeline() {
  auto core = Core::get();
  vk::raii::ShaderModule shaderModule =
      render->createShaderModule(readFile("shaders/outline.spv"));

  vk::PipelineShaderStageCreateInfo shaderStages[] = {
      {.stage = vk::ShaderStageFlagBits::eVertex,
       .module = *shaderModule,
       .pName = "outlineVert"},
      {.stage = vk::ShaderStageFlagBits::eFragment,
       .module = *shaderModule,
       .pName = "outlineFrag"}};

  // No vertex buffer: outlineVert synthesizes a fullscreen triangle.
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      .topology = vk::PrimitiveTopology::eTriangleList};
  vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                    .scissorCount = 1};
  vk::PipelineRasterizationStateCreateInfo rasterizer{
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eNone,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .lineWidth = 1.0f};
  vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = core->graphics->msaaSamples};
  // Off both ways: the edges are found in the G-buffer, not by depth here.
  vk::PipelineDepthStencilStateCreateInfo depthStencil{
      .depthTestEnable = vk::False, .depthWriteEnable = vk::False};
  // The original's `col -= deltas.x - deltas.y`: reverse subtract with both
  // factors One gives dst - src, and the alpha channel is left as it was.
  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::True,
      .srcColorBlendFactor = vk::BlendFactor::eOne,
      .dstColorBlendFactor = vk::BlendFactor::eOne,
      .colorBlendOp = vk::BlendOp::eReverseSubtract,
      .srcAlphaBlendFactor = vk::BlendFactor::eZero,
      .dstAlphaBlendFactor = vk::BlendFactor::eOne,
      .alphaBlendOp = vk::BlendOp::eAdd,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
  vk::PipelineColorBlendStateCreateInfo colorBlending{
      .attachmentCount = 1, .pAttachments = &colorBlendAttachment};
  std::vector dynamicStates = {vk::DynamicState::eViewport,
                               vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState{
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data()};

  vk::PushConstantRange pushRange{.stageFlags =
                                      vk::ShaderStageFlagBits::eFragment,
                                  .offset = 0,
                                  .size = sizeof(OutlinePushConstants)};
  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
      .setLayoutCount = 1,
      .pSetLayouts = &*outlineDescriptorSetLayout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushRange};
  outlinePipelineLayout =
      vk::raii::PipelineLayout(core->device->device, pipelineLayoutInfo);

  vk::Format depthFormat = core->graphics->findDepthFormat();
  vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                     vk::PipelineRenderingCreateInfo>
      chain = {{.stageCount = 2,
                .pStages = shaderStages,
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *outlinePipelineLayout,
                .renderPass = nullptr},
               {.colorAttachmentCount = 1,
                .pColorAttachmentFormats =
                    &core->graphics->swapChainSurfaceFormat.format,
                .depthAttachmentFormat = depthFormat}};

  outlinePipeline =
      vk::raii::Pipeline(core->device->device, nullptr,
                         chain.get<vk::GraphicsPipelineCreateInfo>());
}

void OutlinePlugin::createOutlineDescriptorSet() {
  auto core = Core::get();
  std::array poolSize{vk::DescriptorPoolSize(
                          vk::DescriptorType::eSampledImage, 1),
                      vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 1)};
  vk::DescriptorPoolCreateInfo poolInfo{
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
      .pPoolSizes = poolSize.data()};
  outlineDescriptorPool =
      vk::raii::DescriptorPool(core->device->device, poolInfo);

  vk::DescriptorSetAllocateInfo allocInfo{
      .descriptorPool = *outlineDescriptorPool,
      .descriptorSetCount = 1,
      .pSetLayouts = &*outlineDescriptorSetLayout};
  outlineDescriptorSets =
      vk::raii::DescriptorSets(core->device->device, allocInfo);
  outlineBoundView = nullptr; // written on the first prepass
}

void OutlinePlugin::drawNormalPrepass(entt::registry &reg) {
  auto core = Core::get();
  auto &graphics = core->graphics;
  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;

  // The view is recreated on resize; recreateSwapChain waits for the device to
  // go idle first, so rewriting the set here is safe.
  if (outlineBoundView != *graphics->normalImageView) {
    vk::DescriptorImageInfo imageInfo{
        .imageView = *graphics->normalImageView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo samplerInfo{.sampler = *graphics->gbufferSampler};
    std::array writes{
        vk::WriteDescriptorSet{.dstSet = *outlineDescriptorSets[0],
                               .dstBinding = 1,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eSampledImage,
                               .pImageInfo = &imageInfo},
        vk::WriteDescriptorSet{.dstSet = *outlineDescriptorSets[0],
                               .dstBinding = 2,
                               .descriptorCount = 1,
                               .descriptorType = vk::DescriptorType::eSampler,
                               .pImageInfo = &samplerInfo}};
    core->device->device.updateDescriptorSets(writes, {});
    outlineBoundView = *graphics->normalImageView;
  }

  transitionImageLayout(
      commandBuffer, *graphics->normalImage, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal, {},
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::ImageAspectFlagBits::eColor);
  transitionImageLayout(commandBuffer, *graphics->normalDepthImage,
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eDepthAttachmentOptimal,
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits2::eLateFragmentTests,
                        vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits2::eLateFragmentTests,
                        vk::ImageAspectFlagBits::eDepth);

  // rgb is a unit vector so open sky reads as one flat normal (the shader's
  // dot-product term stays 0 there); a = -1 is the original's "ray missed",
  // which is what turns every silhouette into a large distance jump.
  vk::ClearValue clearColor =
      vk::ClearColorValue(0.0f, 0.0f, 1.0f, -1.0f);
  vk::ClearValue clearDepth = vk::ClearDepthStencilValue{0.0f, 0}; // reversed-Z
  vk::RenderingAttachmentInfo colorAttachment{
      .imageView = *graphics->normalImageView,
      .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eStore,
      .clearValue = clearColor};
  vk::RenderingAttachmentInfo depthAttachment{
      .imageView = *graphics->normalDepthImageView,
      .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eDontCare,
      .clearValue = clearDepth};
  vk::RenderingInfo renderingInfo{
      .renderArea = {.offset = {0, 0}, .extent = frame.extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachment,
      .pDepthAttachment = &depthAttachment};

  commandBuffer.beginRendering(renderingInfo);
  commandBuffer.setViewport(
      0, vk::Viewport(0.0f, 0.0f, static_cast<float>(frame.extent.width),
                      static_cast<float>(frame.extent.height), 0.0f, 1.0f));
  commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), frame.extent));
  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *normalPipeline);
  // DebugMesh entities are translucent diagnostics, not surfaces - outlining
  // them would trace the gizmos instead of the world.
  for (auto [entity, meshRef, renderable] :
       reg.view<MeshRef, Renderable>(entt::exclude<DebugMesh>).each()) {
    drawRenderable(commandBuffer, render->pipelineLayout, frame.frameIndex,
                   *meshRef.mesh, renderable);
  }
  commandBuffer.endRendering();

  transitionImageLayout(commandBuffer, *graphics->normalImage,
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::AccessFlagBits2::eColorAttachmentWrite,
                        vk::AccessFlagBits2::eShaderRead,
                        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits2::eFragmentShader,
                        vk::ImageAspectFlagBits::eColor);
}

void OutlinePlugin::drawOutline(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;

  outlinePush.resolution = {static_cast<float>(frame.extent.width),
                            static_cast<float>(frame.extent.height)};

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *outlinePipeline);
  commandBuffer.setViewport(
      0, vk::Viewport(0.0f, 0.0f, static_cast<float>(frame.extent.width),
                      static_cast<float>(frame.extent.height), 0.0f, 1.0f));
  commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), frame.extent));
  commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   *outlinePipelineLayout, 0,
                                   *outlineDescriptorSets[0], nullptr);
  commandBuffer.pushConstants<OutlinePushConstants>(
      *outlinePipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
      outlinePush);
  commandBuffer.draw(3, 1, 0, 0);
}
