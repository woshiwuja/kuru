#include "render.hpp"
#include "../lighting/lighting.hpp"
#include "mesh_registry.hpp"
#include "model/model.hpp"
#include "plugin/plugin.hpp"
#include <Kuru.h>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include "../material/material.hpp"

using namespace KR;

void RenderPlugin::init(entt::registry &reg) {
  // Other plugins reach the renderer through the registry, not a global.
  reg.ctx().emplace<RenderPlugin *>(this);
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createDescriptorPool();
  registerComponent<Renderable>();
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
      .rasterizationSamples = core->graphics->msaaSamples,
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

  rasterizer.cullMode = vk::CullModeFlagBits::eNone; // see both sides of a poly
  depthStencil.depthWriteEnable = vk::False; // translucent: test, don't occlude
  colorBlendAttachment = vk::PipelineColorBlendAttachmentState{
      .blendEnable = vk::True,
      .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
      .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
      .colorBlendOp = vk::BlendOp::eAdd,
      .srcAlphaBlendFactor = vk::BlendFactor::eOne,
      .dstAlphaBlendFactor = vk::BlendFactor::eZero,
      .alphaBlendOp = vk::BlendOp::eAdd,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  debugPipeline = vk::raii::Pipeline(
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

// Runs after drawFrame has waited this slot's fence. Anything retired at frame
// N was used by frames <= N at most, and frame N's fence has been waited once
// MAX_FRAMES_IN_FLIGHT more frames have started.
void RenderPlugin::start(entt::registry &reg) {
  ++frameCount;
  std::erase_if(retired, [this](const Retired &r) {
    return r.frame + MAX_FRAMES_IN_FLIGHT <= frameCount;
  });
}

void RenderPlugin::update(entt::registry &reg) {
  auto *core = Core::get();
  updateUniforms(reg);
  drawMeshes(reg);
  ImGui::ShowDemoWindow();
  if (ImGui::Begin("Scene")) {
    ImGui::Text("%.1f fps (%.2f ms)", 1.0f / std::max(core->deltaTime(), 1e-6f),
                core->deltaTime() * 1000.0f);
    ImGui::Text("drawables: %zu", reg.view<MeshRef>().size());
    ImGui::Text("swapchain: %ux%u", core->graphics->swapChainExtent.width,
                core->graphics->swapChainExtent.height);
  }
  ImGui::End();
}

void RenderPlugin::spawn(entt::registry &reg, entt::entity entity,
                         std::shared_ptr<Mesh> mesh,
                         std::shared_ptr<Texture> texture, glm::vec4 params,
                         Transform transform) {
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
      vk::DescriptorBufferInfo bufferInfo{.buffer =
                                              *renderable.uniformBuffers[i],
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
  reg.emplace<MaterialRef>(
      entity, std::make_shared<Material>(Material{.baseColor = std::move(texture)}),
      params);
  reg.emplace<Renderable>(entity, std::move(renderable));
}

void RenderPlugin::despawn(entt::registry &reg, entt::entity entity) {
  Retired r{.frame = frameCount};
  if (auto *c = reg.try_get<Renderable>(entity))
    r.renderable = std::move(*c);
  if (auto *c = reg.try_get<MeshRef>(entity))
    r.mesh = std::move(c->mesh);
  if (auto *c = reg.try_get<MaterialRef>(entity))
    r.material = std::move(c->material);
  retired.push_back(std::move(r));
  reg.destroy(entity);
}

void RenderPlugin::updateUniforms(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();

  // The components are already in UBO layout, so gathering is a plain copy.
  DirectionalLight dirLights[MAX_LIGHTS];
  PointLight       pointLights[MAX_LIGHTS];
  SpotLight        spotLights[MAX_LIGHTS];
  glm::uvec4 counts{0};

  for (auto [light_e, light] : reg.view<DirectionalLight>().each()) {
    if (counts.x >= MAX_LIGHTS) {
      break;
    }
    dirLights[counts.x++] = light;
  }
  for (auto [light_e, light] : reg.view<PointLight>().each()) {
    if (counts.y >= MAX_LIGHTS) {
      break;
    }
    pointLights[counts.y++] = light;
  }
  for (auto [light_e, light] : reg.view<SpotLight>().each()) {
    if (counts.z >= MAX_LIGHTS) {
      break;
    }
    spotLights[counts.z++] = light;
  }

  // Column 3 of the inverse view is the camera's world position. Once per
  // frame: the specular term needs it and the shader cannot invert `view`.
  const glm::vec4 cameraPos = glm::inverse(frame.view)[3];

  for (auto [entity, transform, material, renderable] :
       reg.view<Transform, MaterialRef, Renderable>().each()) {
    const Material &m = *material.material;
    UniformBufferObject ubo{
        .model = transform.matrix(),
        .view = frame.view,
        .proj = frame.proj,
        .material = {material.params.x, material.params.y, m.alphaCutoff,
                     m.alphaMask ? 1.0f : 0.0f},
        .baseColor = m.color,
        .pbr = {m.metallic, m.roughness, 0.0f, 0.0f},
        .cameraPos = cameraPos,
        .counts = counts};
    std::copy(dirLights, dirLights + counts.x, ubo.dirLights);
    std::copy(pointLights, pointLights + counts.y, ubo.pointLights);
    std::copy(spotLights, spotLights + counts.z, ubo.spotLights);
    memcpy(renderable.uniformBuffersMapped[frame.frameIndex], &ubo,
           sizeof(ubo));
  }
}

// One entity's submeshes, bound and drawn through `layout`. Shared by the main
// pass and the outline's normal prepass, which reuse the same descriptor sets.
void drawRenderable(const vk::raii::CommandBuffer &commandBuffer,
                    const vk::raii::PipelineLayout &layout, uint32_t frameIndex,
                    const Mesh &mesh, const Renderable &renderable) {
  commandBuffer.bindVertexBuffers(0, *mesh.vertexBuffer, {0});
  commandBuffer.bindIndexBuffer(*mesh.indexBuffer, 0, vk::IndexType::eUint32);

  if (mesh.submeshes.empty()) {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *layout, 0,
        *renderable.descriptorSets[0][frameIndex], nullptr);
    commandBuffer.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
    return;
  }

  for (size_t s = 0; s < mesh.submeshes.size(); s++) {
    const SubMesh &sub = mesh.submeshes[s];
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *layout, 0,
        *renderable.descriptorSets[s][frameIndex], nullptr);
    commandBuffer.drawIndexed(sub.indexCount, 1, sub.indexOffset, 0, 0);
  }
}

void RenderPlugin::drawMeshes(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;

  commandBuffer.setViewport(
      0, vk::Viewport(0.0f, 0.0f, static_cast<float>(frame.extent.width),
                      static_cast<float>(frame.extent.height), 0.0f, 1.0f));
  commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), frame.extent));

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                             *graphicsPipeline);
  for (auto [entity, meshRef, renderable] :
       reg.view<MeshRef, Renderable>(entt::exclude<DebugMesh>).each()) {
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex,
                   *meshRef.mesh, renderable);
  }

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *debugPipeline);
  for (auto [entity, meshRef, renderable] :
       reg.view<MeshRef, Renderable, DebugMesh>().each()) {
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex,
                   *meshRef.mesh, renderable);
  }
}

namespace {
struct TextureLoader {
  using result_type = std::shared_ptr<Texture>;
  result_type operator()(const std::string &path) const {
    return loadTexture(path);
  }
};
using TextureCache = entt::resource_cache<Texture, TextureLoader>;
} // namespace

std::shared_ptr<Texture> getTexture(entt::registry &reg,
                                    const std::string &path) {
  auto *cache = reg.ctx().find<TextureCache>();
  if (cache == nullptr) {
    cache = &reg.ctx().emplace<TextureCache>();
  }
  return cache->load(entt::hashed_string::value(path.c_str()), path)
      .first->second.handle();
}

void spawn(entt::registry &reg, entt::entity entity,
           const std::string &meshPath, const std::string &texturePath,
           glm::vec4 params, Transform transform) {
  std::shared_ptr<Texture> fallbackTexture = getTexture(reg, texturePath);
  renderer(reg).spawn(reg, entity, getMesh(reg, meshPath).handle(),
                      std::move(fallbackTexture), params, transform);
}
