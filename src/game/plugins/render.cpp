#include "render.hpp"
#include "../../lib/common/common.hpp"
#include "../../lib/core/core.hpp"
#include <glm/gtc/matrix_transform.hpp>

glm::mat4 Transform::matrix() const {
	glm::mat4 model = glm::mat4(1.0f);
	model           = glm::translate(model, position);
	model           = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
	model           = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
	model           = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
	model           = glm::scale(model, scale);
	return model;
}

void RenderPlugin::init(entt::registry &reg) {
  // Other plugins reach the renderer through the registry, not a global.
  reg.ctx().emplace<RenderPlugin *>(this);
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createDescriptorPool();
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

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
      .setLayoutCount = 1,
      .pSetLayouts = &*descriptorSetLayout,
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

void RenderPlugin::run(entt::registry &reg) {
  updateUniforms(reg);
  drawMeshes(reg);
}

entt::entity RenderPlugin::spawn(entt::registry &reg, std::shared_ptr<Mesh> mesh,
                                 std::shared_ptr<Texture> texture,
                                 glm::vec4 params, Transform transform) {
  assert(mesh);
  const entt::entity entity = reg.create();
  reg.emplace<Transform>(entity, transform);
  reg.emplace<MeshRef>(entity, std::move(mesh));
  attach(reg, entity, std::move(texture), params);
  return entity;
}

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

  std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                               *descriptorSetLayout);
  vk::DescriptorSetAllocateInfo allocInfo{
      .descriptorPool = *descriptorPool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data()};
  renderable.descriptorSets =
      core->device->device.allocateDescriptorSets(allocInfo);

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vk::DescriptorBufferInfo bufferInfo{.buffer = *renderable.uniformBuffers[i],
                                        .offset = 0,
                                        .range = sizeof(UniformBufferObject)};
    vk::DescriptorImageInfo imageInfo{
        .sampler = *core->graphics->sampler,
        .imageView = *texture->view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    std::array descriptorWrites{
        vk::WriteDescriptorSet{.dstSet = *renderable.descriptorSets[i],
                               .dstBinding = 0,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eUniformBuffer,
                               .pBufferInfo = &bufferInfo},
        vk::WriteDescriptorSet{
            .dstSet = *renderable.descriptorSets[i],
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &imageInfo}};
    core->device->device.updateDescriptorSets(descriptorWrites, {});
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
  // ponytail: device idle per despawn; per-frame trash buckets if it ever hitches
  Core::get()->device->wait();
  reg.destroy(entity);
}

void RenderPlugin::updateUniforms(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();
  for (auto [entity, transform, material, renderable] :
       reg.view<Transform, MaterialRef, Renderable>().each()) {
    UniformBufferObject ubo{.model = transform.matrix(),
                            .view = frame.view,
                            .proj = frame.proj,
                            .material = material.params};
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

  // ponytail: storage order, one bind pair per entity. Group by MaterialRef
  // (reg.group<MeshRef, MaterialRef>()) when the bind count starts to show.
  for (auto [entity, meshRef, renderable] :
       reg.view<MeshRef, Renderable>().each()) {
    const Mesh &mesh = *meshRef.mesh;
    commandBuffer.bindVertexBuffers(0, *mesh.vertexBuffer, {0});
    commandBuffer.bindIndexBuffer(*mesh.indexBuffer, 0, vk::IndexType::eUint32);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0,
        *renderable.descriptorSets[frame.frameIndex], nullptr);
    commandBuffer.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
  }
}
