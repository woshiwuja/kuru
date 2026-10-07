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
  createShadowResources(); // before any makeRenderable: it binds the map
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
          vk::ShaderStageFlagBits::eFragment, nullptr),
      // The shadow maps (see createShadowResources): sun, then spot/point.
      vk::DescriptorSetLayoutBinding(
          2, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr),
      vk::DescriptorSetLayoutBinding(
          3, vk::DescriptorType::eCombinedImageSampler, 1,
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

  // The shadow pass's light matrix (ShadowPush in slang.slang). Every mesh
  // pipeline shares this layout; only shadowVert reads the range.
  vk::PushConstantRange shadowPushRange{.stageFlags =
                                            vk::ShaderStageFlagBits::eVertex,
                                        .offset = 0,
                                        .size = sizeof(glm::mat4)};
  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{.setLayoutCount = 1,
                                                  .pSetLayouts =
                                                      &*descriptorSetLayout,
                                                  .pushConstantRangeCount = 1,
                                                  .pPushConstantRanges =
                                                      &shadowPushRange};

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

  // Props: same state, plus a per-instance model matrix at binding 1, one
  // vec4 column per location 4-7.
  std::array propBindings{
      bindingDescription,
      vk::VertexInputBindingDescription(1, sizeof(glm::mat4),
                                        vk::VertexInputRate::eInstance)};
  std::vector<vk::VertexInputAttributeDescription> propAttributes(
      attributeDescriptions.begin(), attributeDescriptions.end());
  for (uint32_t c = 0; c < 4; c++) {
    propAttributes.emplace_back(4 + c, 1, vk::Format::eR32G32B32A32Sfloat,
                                c * sizeof(glm::vec4));
  }
  const auto baseVertexInput = vertexInputInfo;
  vertexInputInfo.vertexBindingDescriptionCount = propBindings.size();
  vertexInputInfo.pVertexBindingDescriptions = propBindings.data();
  vertexInputInfo.vertexAttributeDescriptionCount = propAttributes.size();
  vertexInputInfo.pVertexAttributeDescriptions = propAttributes.data();
  shaderStages[0].pName = "vertMainInstanced";
  propPipeline = vk::raii::Pipeline(
      core->device->device, nullptr,
      pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
  vertexInputInfo = baseVertexInput;
  shaderStages[0].pName = "vertMain";

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

  rasterizer.polygonMode = vk::PolygonMode::eLine;
  depthStencil.depthTestEnable = vk::False;
  debugWirePipeline = vk::raii::Pipeline(
      core->device->device, nullptr,
      pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void RenderPlugin::createDescriptorPool() {
  // We need MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT descriptor sets
  std::array poolSize{
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,
                             MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT),
      // Three per set: the texture and the two shadow maps.
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,
                             3 * MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT)};
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
  drawShadowPass(reg);

}

void RenderPlugin::update(entt::registry &reg) {
  auto *core = Core::get();
  updateUniforms(reg);
  fillMainInstances(reg);
  drawMeshes(reg);
  ImGui::ShowDemoWindow();
  static std::array<float, 120> frameMs{}, fps{};
  static int head = 0;
  const float dt = std::max(ImGui::GetIO().DeltaTime, 1e-6f);
  frameMs[head] = dt * 1000.0f;
  fps[head] = 1.0f / dt;
  head = (head + 1) % frameMs.size();
  if (ImGui::Begin("Scene")) {
    ImGui::Text("%.1f fps (%.2f ms)", 1.0f / dt, dt * 1000.0f);
    const ImVec2 size(-1.0f, 60.0f); // full width
    ImGui::PlotHistogram("##frame ms", frameMs.data(), frameMs.size(), head,
                         "frame time (ms)", 0.0f, FLT_MAX, size);
    ImGui::PlotLines("##fps", fps.data(), fps.size(), head, "fps", 0.0f,
                     FLT_MAX, size);
    uint32_t propsDrawn = 0;
    for (const auto &[key, batch] : propBatches)
      propsDrawn += batch.count; // camera-visible, from fillMainInstances
    ImGui::Text("drawables: %zu (%u drawn: %u entities + %u props)",
                reg.view<MeshRef>().size(), mainDraws + propsDrawn, mainDraws,
                propsDrawn);
    ImGui::Text("swapchain: %ux%u", core->graphics->swapChainExtent.width,
                core->graphics->swapChainExtent.height);
  }
  ImGui::End();

  if (ImGui::Begin("Shadows")) {
    auto &c = shadowConfig;
    ImGui::Checkbox("enabled", &c.enabled);
    // Bigger box = more scene covered, blurrier shadows (fixed 2048 texels).
    ImGui::DragFloat("half extent", &c.halfExtent, 0.5f, 1.0f, 500.0f);
    ImGui::DragFloat("depth range", &c.depthRange, 1.0f, 1.0f, 2000.0f);
    // Acne (striped self-shadowing) -> raise; shadows detaching from their
    // casters (peter-panning) -> lower.
    ImGui::DragFloat("bias const", &c.biasConst, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("bias slope", &c.biasSlope, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("shader bias", &c.shaderBias, 0.0001f, 0.0f, 0.05f,
                     "%.4f");
    ImGui::SeparatorText("spot + point");
    ImGui::Checkbox("local enabled", &c.localEnabled);
    ImGui::DragFloat("spot far", &c.spotFar, 0.5f, 1.0f, 500.0f);
    ImGui::DragFloat("local shader bias", &c.localShaderBias, 0.00005f, 0.0f,
                     0.01f, "%.5f");
    ImGui::Text("shadowed: %u spot, %u point (%u passes)", spotShadowCount,
                pointShadowCount, 1 + spotShadowCount + 6 * pointShadowCount);
    ImGui::Text("shadow draws: %u (%zu casters, culled per pass)",
                shadowDraws, shadowCasters.size());
  }
  ImGui::End();

}

// The material editor's values: the rest of a Material is textures and GPU
// state that re-spawning from Model rebuilds.
struct MaterialValues {
  glm::vec4 color;
  float metallic, roughness, alphaCutoff;
  bool alphaMask;
};

void RenderPlugin::save(entt::registry &reg, SaveFile &file) {
  auto view = reg.view<Model, MaterialRef>();
  // Exact count: a multi-type view's size_hint() is only an upper bound.
  file(static_cast<uint32_t>(std::distance(view.begin(), view.end())));
  for (auto [e, model, ref] : view.each()) {
    const Material &m = *ref.material;
    file(e);
    file(MaterialValues{m.color, m.metallic, m.roughness, m.alphaCutoff,
                        m.alphaMask});
  }
}

// Loaded entities have Transform and Model but none of the GPU-side
// components: spawn them again from the saved paths.
void RenderPlugin::load(entt::registry &reg, SaveFile &file) {
  auto view = reg.view<Model>();
  const std::vector<entt::entity> entities(view.begin(), view.end());
  for (entt::entity e : entities) {
    const Model model = reg.get<Model>(e); // copy: spawn() replaces it
    if (model.prop)
      ::spawnProp(reg, e, model.mesh, model.texture);
    else
      ::spawn(reg, e, model.mesh, model.texture, model.params);
  }
  uint32_t count = 0;
  file(count);
  while (count--) {
    entt::entity e;
    MaterialValues v;
    file(e);
    file(v);
    if (auto *ref = reg.try_get<MaterialRef>(e)) {
      Material &m = *ref->material;
      m.color = v.color;
      m.metallic = v.metallic;
      m.roughness = v.roughness;
      m.alphaCutoff = v.alphaCutoff;
      m.alphaMask = v.alphaMask;
    }
  }
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

void RenderPlugin::spawnProp(entt::registry &reg, entt::entity entity,
                             std::shared_ptr<Mesh> mesh,
                             std::shared_ptr<Texture> texture,
                             Transform transform) {
  assert(mesh && texture);
  propBatch(mesh, texture);
  if (!reg.all_of<Transform>(entity)) {
    reg.emplace<Transform>(entity, transform);
  }
  reg.emplace<MeshRef>(entity, std::move(mesh));
  reg.emplace_or_replace<Prop>(entity); // already there on a loaded prop
}

PropBatch &RenderPlugin::propBatch(const std::shared_ptr<Mesh> &mesh,
                                   const std::shared_ptr<Texture> &texture,
                                   std::shared_ptr<Material> material) {
  auto [it, fresh] = propBatches.try_emplace(mesh.get());
  if (fresh) {
    it->second.mesh = mesh;
    if (!material) {
      material = std::make_shared<Material>(Material{.baseColor = texture});
    }
    it->second.material = std::move(material);
    it->second.renderable = makeRenderable(*mesh, texture);
  }
  return it->second;
}

// Requires MeshRef to already be on `entity` - RenderPlugin::spawn emplaces
// it right before calling this.
void RenderPlugin::attach(entt::registry &reg, entt::entity entity,
                          std::shared_ptr<Texture> texture, glm::vec4 params) {
  assert(texture);
  Renderable renderable = makeRenderable(*reg.get<MeshRef>(entity).mesh, texture);
  if (!reg.all_of<Transform>(entity)) {
    reg.emplace<Transform>(entity);
  }
  reg.emplace<MaterialRef>(
      entity, std::make_shared<Material>(Material{.baseColor = std::move(texture)}),
      params);
  reg.emplace<Renderable>(entity, std::move(renderable));
}

Renderable RenderPlugin::makeRenderable(const Mesh &mesh,
                                        const std::shared_ptr<Texture> &texture) {
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
      vk::DescriptorImageInfo shadowInfo{
          .sampler = *shadowSampler,
          .imageView = *shadowView,
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      vk::DescriptorImageInfo localShadowInfo{
          .sampler = *shadowSampler,
          .imageView = *localShadowArrayView,
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
                                 .pImageInfo = &imageInfo},
          vk::WriteDescriptorSet{.dstSet = *renderable.descriptorSets[s][i],
                                 .dstBinding = 2,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &shadowInfo},
          vk::WriteDescriptorSet{.dstSet = *renderable.descriptorSets[s][i],
                                 .dstBinding = 3,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType =
                                     vk::DescriptorType::eCombinedImageSampler,
                                 .pImageInfo = &localShadowInfo}};
      core->device->device.updateDescriptorSets(descriptorWrites, {});
    }
  }
  return renderable;
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

// ---- shadows ----------------------------------------------------------------
// A depth-only pass from dirLights[0] into shadowImage, sampled with a
// comparison sampler at binding 2 of every mesh descriptor set. Standard Z
// here (clear 1, nearer = smaller), unlike the reversed-Z main pass: the map is
// its own projection and the shader compares against it directly.

void RenderPlugin::createShadowResources() {
  auto core = Core::get();
  auto &graphics = core->graphics;
  graphics->createImage(shadowSize, shadowSize, shadowFormat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eDepthStencilAttachment |
                            vk::ImageUsageFlagBits::eSampled,
                        vk::MemoryPropertyFlagBits::eDeviceLocal, shadowImage,
                        shadowMemory);
  shadowView = graphics->createImageView(shadowImage, shadowFormat,
                                         vk::ImageAspectFlagBits::eDepth);

  graphics->createImage(localShadowSize, localShadowSize, shadowFormat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eDepthStencilAttachment |
                            vk::ImageUsageFlagBits::eSampled,
                        vk::MemoryPropertyFlagBits::eDeviceLocal,
                        localShadowImage, localShadowMemory,
                        vk::SampleCountFlagBits::e1, localShadowLayers);
  vk::ImageViewCreateInfo viewInfo{
      .image = *localShadowImage,
      .viewType = vk::ImageViewType::e2DArray,
      .format = shadowFormat,
      .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0,
                           localShadowLayers}};
  localShadowArrayView = vk::raii::ImageView(core->device->device, viewInfo);
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.subresourceRange.layerCount = 1;
  for (uint32_t layer = 0; layer < localShadowLayers; layer++) {
    viewInfo.subresourceRange.baseArrayLayer = layer;
    localShadowLayerViews.emplace_back(core->device->device, viewInfo);
  }

  // Linear + compare: each tap is hardware 2x2 PCF. Off the map is white =
  // depth 1 = never in shadow.
  vk::SamplerCreateInfo samplerInfo{
      .magFilter = vk::Filter::eLinear,
      .minFilter = vk::Filter::eLinear,
      .mipmapMode = vk::SamplerMipmapMode::eNearest,
      .addressModeU = vk::SamplerAddressMode::eClampToBorder,
      .addressModeV = vk::SamplerAddressMode::eClampToBorder,
      .addressModeW = vk::SamplerAddressMode::eClampToBorder,
      .compareEnable = vk::True,
      .compareOp = vk::CompareOp::eLessOrEqual,
      .borderColor = vk::BorderColor::eFloatOpaqueWhite};
  shadowSampler = vk::raii::Sampler(core->device->device, samplerInfo);

  vk::raii::ShaderModule shaderModule =
      createShaderModule(readFile("shaders/slang.spv"));
  vk::PipelineShaderStageCreateInfo stage{
      .stage = vk::ShaderStageFlagBits::eVertex,
      .module = *shaderModule,
      .pName = "shadowVert"};
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
  // No culling: the light matrix isn't Y-flipped like the camera's, so the
  // winding is mirrored, and thin one-sided geometry should cast either way.
  // The slope bias is what keeps lit faces from shadowing themselves (acne).
  vk::PipelineRasterizationStateCreateInfo rasterizer{
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eNone,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .depthBiasEnable = vk::True,
      .lineWidth = 1.0f};
  vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = vk::SampleCountFlagBits::e1};
  vk::PipelineDepthStencilStateCreateInfo depthStencil{
      .depthTestEnable = vk::True,
      .depthWriteEnable = vk::True,
      .depthCompareOp = vk::CompareOp::eLess};
  vk::PipelineColorBlendStateCreateInfo colorBlending{.attachmentCount = 0};
  std::vector dynamicStates = {vk::DynamicState::eViewport,
                               vk::DynamicState::eScissor,
                               vk::DynamicState::eDepthBias};
  vk::PipelineDynamicStateCreateInfo dynamicState{
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data()};

  // The mesh layout: shadowVert reads the same per-entity UBO at binding 0.
  vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                     vk::PipelineRenderingCreateInfo>
      chain = {{.stageCount = 1,
                .pStages = &stage,
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
               {.colorAttachmentCount = 0,
                .depthAttachmentFormat = shadowFormat}};
  shadowPipeline = vk::raii::Pipeline(
      core->device->device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());

  // Props: the same, plus the per-instance matrix at binding 1 (as in
  // createGraphicsPipeline's propPipeline).
  std::array propBindings{
      bindingDescription,
      vk::VertexInputBindingDescription(1, sizeof(glm::mat4),
                                        vk::VertexInputRate::eInstance)};
  std::vector<vk::VertexInputAttributeDescription> propAttributes(
      attributeDescriptions.begin(), attributeDescriptions.end());
  for (uint32_t c = 0; c < 4; c++) {
    propAttributes.emplace_back(4 + c, 1, vk::Format::eR32G32B32A32Sfloat,
                                c * sizeof(glm::vec4));
  }
  vertexInputInfo.vertexBindingDescriptionCount = propBindings.size();
  vertexInputInfo.pVertexBindingDescriptions = propBindings.data();
  vertexInputInfo.vertexAttributeDescriptionCount = propAttributes.size();
  vertexInputInfo.pVertexAttributeDescriptions = propAttributes.data();
  stage.pName = "shadowVertInstanced";
  shadowPropPipeline = vk::raii::Pipeline(
      core->device->device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
}

bool RenderPlugin::shadowMatrix(entt::registry &reg, glm::vec3 center,
                                glm::mat4 &out) const {
  // The first one: the same light updateUniforms copies into dirLights[0].
  auto lights = reg.view<DirectionalLight>();
  if (lights.begin() == lights.end())
    return false;
  // direction points at the light (it's the shader's `l`), so the light's
  // rays travel along -l.
  const glm::vec3 l = glm::normalize(
      glm::vec3(lights.get<DirectionalLight>(*lights.begin()).direction));
  glm::vec3 up(0, 1, 0);
  if (std::abs(l.y) > 0.99f) {
    up = glm::vec3(1, 0, 0); // light straight up/down: Y can't be "up"
  }
  const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), -l, up);

  // Snap the box to whole texels in light space, or every camera step
  // resamples the map and shadow edges crawl.
  const float s = shadowConfig.halfExtent, d = shadowConfig.depthRange;
  const float texel = 2.0f * s / shadowSize;
  glm::vec3 c = view * glm::vec4(center, 1.0f);
  c.x = std::floor(c.x / texel) * texel;
  c.y = std::floor(c.y / texel) * texel;
  // View space looks down -z, so the box spans distances -c.z +- d.
  out = glm::orthoRH_ZO(c.x - s, c.x + s, c.y - s, c.y + s, -c.z - d, -c.z + d) *
        view;
  return true;
}

// Light matrices for this frame's passes, in the order updateUniforms gathers
// the lights (view order, MAX_LIGHTS each), so index i here is index i there.
void RenderPlugin::updateShadowMatrices(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();
  // Centred halfExtent ahead of the camera, not on it: the box then spans
  // from the camera to 2 * halfExtent along the view, which is what's on
  // screen. Centred on the camera, half of it was behind and a distant view
  // got no shadows at all.
  const glm::mat4 cameraWorld = glm::inverse(frame.view);
  const glm::vec3 forward = -glm::normalize(glm::vec3(cameraWorld[2]));
  sunShadowValid =
      shadowConfig.enabled &&
      shadowMatrix(reg,
                   glm::vec3(cameraWorld[3]) + forward * shadowConfig.halfExtent,
                   sunViewProj);

  spotShadowCount = pointShadowCount = 0;
  if (!shadowConfig.localEnabled)
    return;
  constexpr float nearPlane = 0.05f;
  const auto lookFrom = [](glm::vec3 pos, glm::vec3 dir) {
    glm::vec3 up(0, 1, 0);
    if (std::abs(dir.y) > 0.99f) {
      up = glm::vec3(0, 0, 1); // looking straight up/down: Y can't be "up"
    }
    return glm::lookAt(pos, pos + dir, up);
  };
  for (auto [e, light] : reg.view<SpotLight>().each()) {
    if (spotShadowCount >= MAX_LIGHTS)
      break;
    // direction.w is the outer cone's radius at unit distance = tan(half
    // angle). A little margin so the 3x3 filter stays inside the map.
    const float fov = std::clamp(2.0f * std::atan(light.direction.w) * 1.05f,
                                 glm::radians(1.0f), glm::radians(170.0f));
    spotViewProj[spotShadowCount++] =
        glm::perspectiveRH_ZO(fov, 1.0f, nearPlane, shadowConfig.spotFar) *
        lookFrom(glm::vec3(light.position),
                 glm::normalize(glm::vec3(light.direction)));
  }
  // +X -X +Y -Y +Z -Z: the order pointShadow() picks faces in. 95 degrees,
  // not 90: the margin keeps filter taps near a face edge on the map instead
  // of on the border (which reads as lit).
  static const glm::vec3 cubeFaces[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                         {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  for (auto [e, light] : reg.view<PointLight>().each()) {
    if (pointShadowCount >= MAX_LIGHTS)
      break;
    const glm::mat4 proj = glm::perspectiveRH_ZO(
        glm::radians(95.0f), 1.0f, nearPlane,
        std::max(light.position.w, nearPlane * 2.0f));
    for (int f = 0; f < 6; f++) {
      pointViewProj[pointShadowCount * 6 + f] =
          proj * lookFrom(glm::vec3(light.position), cubeFaces[f]);
    }
    pointShadowCount++;
  }
}

// One depth-only render of every mesh into `view` (cleared to 1 = lit).
void RenderPlugin::shadowLayerPass(entt::registry &reg, vk::ImageView view,
                                   uint32_t size, const glm::mat4 *viewProj,
                                   uint32_t pass) {
  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;
  const vk::Extent2D extent{size, size};
  vk::RenderingAttachmentInfo depthAttachment{
      .imageView = view,
      .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eStore,
      .clearValue = vk::ClearDepthStencilValue{1.0f, 0}};
  commandBuffer.beginRendering({.renderArea = {.offset = {0, 0}, .extent = extent},
                                .layerCount = 1,
                                .pDepthAttachment = &depthAttachment});
  if (!viewProj) {
    commandBuffer.endRendering();
    return;
  }
  commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, float(size),
                                            float(size), 0.0f, 1.0f));
  commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
  commandBuffer.setDepthBias(shadowConfig.biasConst, 0.0f,
                             shadowConfig.biasSlope);
  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *shadowPipeline);
  commandBuffer.pushConstants<glm::mat4>(
      *pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, *viewProj);

  // A caster whose bounding sphere is outside this view can't put anything
  // in this layer.
  const Frustum frustum(*viewProj);
  for (const ShadowCaster &c : shadowCasters) {
    if (!frustum.visible(c.center, c.radius))
      continue;
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex, *c.mesh,
                   *c.renderable);
    shadowDraws++;
  }

  // Props: this pass's run of already-culled instances (fillShadowInstances).
  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                             *shadowPropPipeline);
  for (auto &[key, batch] : propBatches) {
    const auto [first, count] = batch.shadowSegments[pass];
    if (count == 0)
      continue;
    commandBuffer.bindVertexBuffers(
        1, *batch.shadowInstances[frame.frameIndex].buffer, {0});
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex,
                   *batch.mesh, batch.renderable, count, first);
    shadowDraws++;
  }
  commandBuffer.endRendering();
}

// Both maps are always cleared and transitioned, even with nothing to draw:
// every descriptor set names them in the shader-read layout.
void RenderPlugin::drawShadowPass(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;
  updateShadowMatrices(reg);

  shadowDraws = 0;
  shadowCasters.clear();
  for (auto [e, meshRef, renderable, t] :
       reg.view<MeshRef, Renderable, Transform>(entt::exclude<DebugMesh>)
           .each()) {
    const auto [center, radius] =
        worldBounds(*meshRef.mesh, t.matrix(), t.scale);
    shadowCasters.push_back({meshRef.mesh.get(), &renderable, center, radius});
  }

  // Pass order = segment order: sun, spots, point faces.
  // nullptr = sun off: cleared, nothing drawn.
  std::vector<const glm::mat4 *> passes{nullptr};
  if (sunShadowValid) {
    passes[0] = &sunViewProj;
  }
  for (uint32_t i = 0; i < spotShadowCount; i++)
    passes.push_back(&spotViewProj[i]);
  for (uint32_t i = 0; i < pointShadowCount * 6; i++)
    passes.push_back(&pointViewProj[i]);
  fillShadowInstances(reg, passes);

  // srcStage covers the previous frame's fragment reads of the same images
  // (barriers order against everything submitted earlier on the queue).
  for (vk::Image image : {*shadowImage, *localShadowImage}) {
    transitionImageLayout(commandBuffer, image, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eDepthAttachmentOptimal, {},
                          vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                          vk::PipelineStageFlagBits2::eFragmentShader,
                          vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                              vk::PipelineStageFlagBits2::eLateFragmentTests,
                          vk::ImageAspectFlagBits::eDepth);
  }

  // Cleared even when off; the shader skips it via ubo.shadow.x.
  shadowLayerPass(reg, *shadowView, shadowSize, passes[0], 0);
  for (uint32_t i = 0; i < spotShadowCount; i++) {
    shadowLayerPass(reg, *localShadowLayerViews[i], localShadowSize,
                    passes[1 + i], 1 + i);
  }
  for (uint32_t i = 0; i < pointShadowCount * 6; i++) {
    shadowLayerPass(reg, *localShadowLayerViews[MAX_LIGHTS + i],
                    localShadowSize, passes[1 + spotShadowCount + i],
                    1 + spotShadowCount + i);
  }

  for (vk::Image image : {*shadowImage, *localShadowImage}) {
    transitionImageLayout(commandBuffer, image,
                          vk::ImageLayout::eDepthAttachmentOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                          vk::AccessFlagBits2::eShaderSampledRead,
                          vk::PipelineStageFlagBits2::eLateFragmentTests,
                          vk::PipelineStageFlagBits2::eFragmentShader,
                          vk::ImageAspectFlagBits::eDepth);
  }
}

void RenderPlugin::updateUniforms(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();

  // Column 3 of the inverse view is the camera's world position. Once per
  // frame: the specular term needs it and the shader cannot invert `view`.
  UniformBufferObject ubo{.view = frame.view,
                          .proj = frame.proj,
                          .cameraPos = glm::inverse(frame.view)[3]};

  // The matrices drawShadowPass rendered with this frame; they have to land
  // in every UBO, so they are set before any upload.
  if (sunShadowValid) {
    ubo.lightViewProj = sunViewProj;
    ubo.shadow = {1.0f, 1.0f / shadowSize, shadowConfig.shaderBias, 0.0f};
  }
  std::copy(spotViewProj.begin(), spotViewProj.end(), ubo.spotViewProj);
  std::copy(pointViewProj.begin(), pointViewProj.end(), ubo.pointViewProj);
  ubo.localShadow = {float(spotShadowCount), float(pointShadowCount),
                     1.0f / localShadowSize, shadowConfig.localShaderBias};

  // The components are already in UBO layout, so gathering is a plain copy.
  glm::uvec4 &counts = ubo.counts;
  for (auto [light_e, light] : reg.view<DirectionalLight>().each()) {
    if (counts.x >= MAX_LIGHTS) {
      break;
    }
    ubo.dirLights[counts.x++] = light;
  }
  for (auto [light_e, light] : reg.view<PointLight>().each()) {
    if (counts.y >= MAX_LIGHTS) {
      break;
    }
    ubo.pointLights[counts.y++] = light;
  }
  for (auto [light_e, light] : reg.view<SpotLight>().each()) {
    if (counts.z >= MAX_LIGHTS) {
      break;
    }
    ubo.spotLights[counts.z++] = light;
  }

  auto upload = [&](const Material &m, glm::vec4 params, const glm::mat4 &model,
                    const Renderable &renderable) {
    ubo.model = model;
    ubo.material = {params.x, params.y, m.alphaCutoff, m.alphaMask ? 1.0f : 0.0f};
    ubo.baseColor = m.color;
    ubo.pbr = {m.metallic, m.roughness, 0.0f, 0.0f};
    memcpy(renderable.uniformBuffersMapped[frame.frameIndex], &ubo,
           sizeof(ubo));
  };

  for (auto [entity, transform, material, renderable] :
       reg.view<Transform, MaterialRef, Renderable>().each()) {
    upload(*material.material, material.params, transform.matrix(), renderable);
  }

  // Props share one UBO per batch; their matrices are in the instance
  // buffers fillPropInstances wrote in start().
  for (auto &[key, batch] : propBatches) {
    upload(*batch.material, {}, glm::mat4{1.0f}, batch.renderable);
  }
}

// Grow-only, and replacing is safe: drawFrame has already waited on this
// slot's fence, so nothing in flight still reads it.
static void uploadInstances(PropBatch::InstanceBuffer &inst,
                            const std::vector<glm::mat4> &matrices) {
  const auto n = static_cast<uint32_t>(matrices.size());
  if (n > inst.capacity) {
    inst.capacity = std::max({n, inst.capacity * 2, 16u});
    const vk::DeviceSize size = inst.capacity * sizeof(glm::mat4);
    inst.buffer = nullptr; // release before reallocating
    inst.memory = nullptr;
    createBuffer(size, vk::BufferUsageFlagBits::eVertexBuffer,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 inst.buffer, inst.memory);
    inst.mapped = static_cast<glm::mat4 *>(inst.memory.mapMemory(0, size));
  }
  std::copy(matrices.begin(), matrices.end(), inst.mapped);
}

// The props the camera sees, per batch. In update(), after physics and the
// camera have moved this frame, and nothing recorded yet reads `instances`.
void RenderPlugin::fillMainInstances(entt::registry &reg) {
  const auto &frame = reg.ctx().get<FrameContext>();
  const Frustum camera(frame.proj * frame.view);
  for (auto &[key, batch] : propBatches)
    batch.staging.clear();
  for (auto [e, meshRef, t] : reg.view<Prop, MeshRef, Transform>().each()) {
    const glm::mat4 model = t.matrix();
    const auto [center, radius] = worldBounds(*meshRef.mesh, model, t.scale);
    // No batch yet: Prop was added this frame, ModelPlugin::start makes one.
    const auto batch = propBatches.find(meshRef.mesh.get());
    if (batch != propBatches.end() && camera.visible(center, radius))
      batch->second.staging.push_back(model);
  }
  for (auto &[key, batch] : propBatches) {
    batch.count = static_cast<uint32_t>(batch.staging.size());
    uploadInstances(batch.instances[frame.frameIndex], batch.staging);
  }
}

// One run of visible props per shadow pass, per batch. In start(): the
// shadow draws recorded there need their counts and a buffer that won't be
// replaced later this frame. So shadows use last frame's prop transforms.
void RenderPlugin::fillShadowInstances(
    entt::registry &reg, const std::vector<const glm::mat4 *> &passes) {
  const auto &frame = reg.ctx().get<FrameContext>();
  struct Instance {
    PropBatch *batch;
    glm::mat4  model;
    glm::vec3  center;
    float      radius;
  };
  std::vector<Instance> props;
  for (auto [e, meshRef, t] : reg.view<Prop, MeshRef, Transform>().each()) {
    const auto batch = propBatches.find(meshRef.mesh.get());
    if (batch == propBatches.end())
      continue; // see fillMainInstances
    const glm::mat4 model = t.matrix();
    const auto [center, radius] = worldBounds(*meshRef.mesh, model, t.scale);
    props.push_back({&batch->second, model, center, radius});
  }
  for (auto &[key, batch] : propBatches) {
    batch.staging.clear();
    batch.shadowSegments.assign(passes.size(), {0, 0});
  }
  for (size_t p = 0; p < passes.size(); p++) {
    if (!passes[p])
      continue;
    const Frustum frustum(*passes[p]);
    for (auto &[key, batch] : propBatches)
      batch.shadowSegments[p].first = static_cast<uint32_t>(batch.staging.size());
    for (const Instance &i : props)
      if (frustum.visible(i.center, i.radius))
        i.batch->staging.push_back(i.model);
    for (auto &[key, batch] : propBatches)
      batch.shadowSegments[p].second = static_cast<uint32_t>(
          batch.staging.size() - batch.shadowSegments[p].first);
  }
  for (auto &[key, batch] : propBatches)
    uploadInstances(batch.shadowInstances[frame.frameIndex], batch.staging);
}

// One entity's submeshes, bound and drawn through `layout`. Shared by the main
// pass and the outline's normal prepass, which reuse the same descriptor sets.
void drawRenderable(const vk::raii::CommandBuffer &commandBuffer,
                    const vk::raii::PipelineLayout &layout, uint32_t frameIndex,
                    const Mesh &mesh, const Renderable &renderable,
                    uint32_t instanceCount, uint32_t firstInstance) {
  commandBuffer.bindVertexBuffers(0, *mesh.vertexBuffer, {0});
  commandBuffer.bindIndexBuffer(*mesh.indexBuffer, 0, vk::IndexType::eUint32);

  if (mesh.submeshes.empty()) {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *layout, 0,
        *renderable.descriptorSets[0][frameIndex], nullptr);
    commandBuffer.drawIndexed(mesh.indexCount, instanceCount, 0, 0,
                              firstInstance);
    return;
  }

  for (size_t s = 0; s < mesh.submeshes.size(); s++) {
    const SubMesh &sub = mesh.submeshes[s];
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *layout, 0,
        *renderable.descriptorSets[s][frameIndex], nullptr);
    commandBuffer.drawIndexed(sub.indexCount, instanceCount, sub.indexOffset, 0,
                              firstInstance);
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
  const Frustum camera(frame.proj * frame.view);
  mainDraws = 0;
  for (auto [entity, meshRef, renderable, t] :
       reg.view<MeshRef, Renderable, Transform>(entt::exclude<DebugMesh>)
           .each()) {
    const auto [center, radius] = worldBounds(*meshRef.mesh, t.matrix(), t.scale);
    if (!camera.visible(center, radius))
      continue;
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex,
                   *meshRef.mesh, renderable);
    mainDraws++;
  }

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *propPipeline);
  for (auto &[key, batch] : propBatches) {
    if (batch.count == 0) {
      continue;
    }
    commandBuffer.bindVertexBuffers(
        1, *batch.instances[frame.frameIndex].buffer, {0});
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex,
                   *batch.mesh, batch.renderable, batch.count);
  }

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *debugPipeline);
  for (auto [entity, meshRef, renderable] :
       reg.view<MeshRef, Renderable, DebugMesh>(entt::exclude<DebugWire>).each()) {
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex,
                   *meshRef.mesh, renderable);
  }

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *debugWirePipeline);
  for (auto [entity, meshRef, renderable] :
       reg.view<MeshRef, Renderable, DebugWire>().each()) {
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

static void setModel(entt::registry &reg, entt::entity entity,
                     const std::string &meshPath,
                     const std::string &texturePath, glm::vec4 params,
                     bool prop) {
  assert(meshPath.size() < sizeof(Model::mesh) &&
         texturePath.size() < sizeof(Model::texture));
  Model m{.params = params, .prop = prop};
  snprintf(m.mesh, sizeof(m.mesh), "%s", meshPath.c_str());
  snprintf(m.texture, sizeof(m.texture), "%s", texturePath.c_str());
  reg.emplace_or_replace<Model>(entity, m);
}

void spawn(entt::registry &reg, entt::entity entity,
           const std::string &meshPath, const std::string &texturePath,
           glm::vec4 params, Transform transform) {
  setModel(reg, entity, meshPath, texturePath, params, false);
  std::shared_ptr<Texture> fallbackTexture = getTexture(reg, texturePath);
  renderer(reg).spawn(reg, entity, getMesh(reg, meshPath).handle(),
                      std::move(fallbackTexture), params, transform);
}

void spawnProp(entt::registry &reg, entt::entity entity,
               const std::string &meshPath, const std::string &texturePath,
               Transform transform) {
  setModel(reg, entity, meshPath, texturePath, {}, true);
  renderer(reg).spawnProp(reg, entity, getMesh(reg, meshPath).handle(),
                          getTexture(reg, texturePath), transform);
}
