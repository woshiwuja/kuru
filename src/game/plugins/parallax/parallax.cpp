#include "parallax.hpp"
#include "../picking/picking.hpp"
#include "plugin/default_plugins/render/render.hpp"
#include "plugin/default_plugins/texture/texture.hpp"
#include "../name/name.hpp"
#include "imgui.h"
#include <filesystem>
#include <tracy/Tracy.hpp>

using namespace KR;

// ponytail: a fixed pool, one set per distinct texture with Parallax. Grow
// it (or free unused sets) if a scene ever has more.
static constexpr uint32_t MAX_HEIGHT_SETS = 64;

static void addCustomDraw(entt::registry &reg, entt::entity e) {
  reg.emplace_or_replace<CustomDraw>(e);
}
static void removeCustomDraw(entt::registry &reg, entt::entity e) {
  reg.remove<CustomDraw>(e);
}

void ParallaxPlugin::init(entt::registry &reg) {
  ZoneScoped;
  auto core = Core::get();
  render = &renderer(reg);
  // RenderPlugin's main pass skips these; this plugin draws them instead.
  reg.on_construct<Parallax>().connect<&addCustomDraw>();
  reg.on_destroy<Parallax>().connect<&removeCustomDraw>();
  vk::raii::ShaderModule shaderModule =
      render->createShaderModule(readFile("shaders/parallax.spv"));

  vk::PipelineShaderStageCreateInfo shaderStages[] = {
      {.stage = vk::ShaderStageFlagBits::eVertex,
       .module = *shaderModule,
       .pName = "vertMain"},
      {.stage = vk::ShaderStageFlagBits::eFragment,
       .module = *shaderModule,
       .pName = "parallaxFrag"}};

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
  vk::PipelineMultisampleStateCreateInfo multisampling{
      .rasterizationSamples = core->graphics->msaaSamples};
  // Reversed-Z, like the main pass.
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

  // Set 1: the height map.
  vk::DescriptorSetLayoutBinding heightBinding(
      0, vk::DescriptorType::eCombinedImageSampler, 1,
      vk::ShaderStageFlagBits::eFragment, nullptr);
  heightSetLayout = vk::raii::DescriptorSetLayout(
      core->device->device,
      vk::DescriptorSetLayoutCreateInfo{.bindingCount = 1,
                                        .pBindings = &heightBinding});
  vk::DescriptorPoolSize poolSize(vk::DescriptorType::eCombinedImageSampler,
                                  MAX_HEIGHT_SETS);
  heightPool = vk::raii::DescriptorPool(
      core->device->device,
      vk::DescriptorPoolCreateInfo{
          .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
          .maxSets = MAX_HEIGHT_SETS,
          .poolSizeCount = 1,
          .pPoolSizes = &poolSize});

  // Set 0 is RenderPlugin's layout, so its per-entity sets bind as-is.
  std::array setLayouts{*render->descriptorSetLayout, *heightSetLayout};
  vk::PushConstantRange pushRange{.stageFlags =
                                      vk::ShaderStageFlagBits::eFragment,
                                  .offset = 0,
                                  .size = sizeof(Parallax)};
  vk::PipelineLayoutCreateInfo layoutInfo{
      .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
      .pSetLayouts = setLayouts.data(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushRange};
  pipelineLayout = vk::raii::PipelineLayout(core->device->device, layoutInfo);

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
                .layout = *pipelineLayout,
                .renderPass = nullptr},
               {.colorAttachmentCount = 1,
                .pColorAttachmentFormats =
                    &core->graphics->swapChainSurfaceFormat.format,
                .depthAttachmentFormat = depthFormat}};
  pipeline = vk::raii::Pipeline(core->device->device, nullptr,
                                chain.get<vk::GraphicsPipelineCreateInfo>());

  // Test wall: a 10 m brick quad.
  entt::entity wall = reg.create();
  render->spawn(reg, wall, createQuad(10.0f, 1.0f),
                getTexture(reg, "textures/brick.png"), {},
                Transform{.position = {0, 20, -10}, .scale = {1, 1, 1}});
  Model model{};
  snprintf(model.texture, sizeof(model.texture), "%s", "textures/brick.png");
  reg.emplace<Model>(wall, model); // where height() finds the texture path
  reg.emplace<Parallax>(wall);
  reg.emplace<Pickable>(wall);
  reg.emplace<Name>(wall, Name{.fname = "brick wall"});
}

void ParallaxPlugin::update(entt::registry &reg) {
  ZoneScoped;
  const auto &frame = reg.ctx().get<FrameContext>();
  const auto &commandBuffer = *frame.commandBuffer;
  TracyVkZone(Core::get()->gpuProfiler, *commandBuffer, "parallax");

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
  commandBuffer.setViewport(
      0, vk::Viewport(0.0f, 0.0f, static_cast<float>(frame.extent.width),
                      static_cast<float>(frame.extent.height), 0.0f, 1.0f));
  commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), frame.extent));
  const Frustum camera(frame.proj * frame.view);
  for (auto [e, meshRef, renderable, t, parallax] :
       reg.view<MeshRef, Renderable, Transform, Parallax>(
              entt::exclude<DebugMesh>)
           .each()) {
    const auto [center, radius] =
        worldBounds(*meshRef.mesh, t.matrix(), t.scale);
    if (!camera.visible(center, radius)) {
      continue;
    }
    std::string texturePath;
    if (const auto *model = reg.try_get<Model>(e)) {
      texturePath = model->texture;
    }
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     *pipelineLayout, 1,
                                     *height(reg, texturePath).set, nullptr);
    commandBuffer.pushConstants<Parallax>(
        *pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, parallax);
    drawRenderable(commandBuffer, pipelineLayout, frame.frameIndex,
                   *meshRef.mesh, renderable);
  }

  ImGui::Begin("Parallax");
  for (auto [e, parallax] : reg.view<Parallax>().each()) {
    ImGui::PushID(static_cast<int>(entt::to_integral(e)));
    if (const auto *name = reg.try_get<Name>(e)) {
      ImGui::SeparatorText(name->fname.c_str());
    } else {
      ImGui::SeparatorText("entity");
    }
    ImGui::SliderFloat("depth (m)", &parallax.depth, 0.0f, 0.2f);
    int steps = static_cast<int>(parallax.steps);
    if (ImGui::SliderInt("steps", &steps, 1, 128)) {
      parallax.steps = static_cast<uint32_t>(steps);
    }
    bool invert = parallax.invert != 0;
    if (ImGui::Checkbox("invert (dark = raised)", &invert)) {
      if (invert) {
        parallax.invert = 1;
      } else {
        parallax.invert = 0;
      }
    }
    ImGui::SliderFloat("bump", &parallax.bump, 0.0f, 4.0f);
    ImGui::SliderFloat("shadow", &parallax.shadow, 0.0f, 32.0f);
    ImGui::SliderFloat("fade distance", &parallax.fadeDist, 1.0f, 200.0f);
    bool silhouette = parallax.silhouette != 0;
    if (ImGui::Checkbox("silhouette", &silhouette)) {
      if (silhouette) {
        parallax.silhouette = 1;
      } else {
        parallax.silhouette = 0;
      }
    }
    ImGui::PopID();
  }
  ImGui::End();
}

const ParallaxPlugin::Height &ParallaxPlugin::height(entt::registry &reg,
                                                     const std::string &texturePath) {
  auto it = heights.find(texturePath);
  if (it != heights.end()) {
    return it->second;
  }
  ZoneScoped;
  // textures/brick.png -> textures/brick_height.png; none = the albedo itself.
  std::filesystem::path p(texturePath);
  const std::string heightPath =
      (p.parent_path() / (p.stem().string() + "_height.png")).generic_string();
  std::string path = texturePath;
  if (!texturePath.empty() && std::filesystem::exists(assetPath(heightPath))) {
    path = heightPath;
  }
  Height h{.texture = getTexture(reg, path)};

  auto &device = Core::get()->device->device;
  vk::DescriptorSetAllocateInfo allocInfo{.descriptorPool = *heightPool,
                                          .descriptorSetCount = 1,
                                          .pSetLayouts = &*heightSetLayout};
  h.set = std::move(vk::raii::DescriptorSets(device, allocInfo).front());
  vk::DescriptorImageInfo imageInfo{
      .sampler = *Core::get()->graphics->sampler,
      .imageView = *h.texture->view,
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
  device.updateDescriptorSets(
      vk::WriteDescriptorSet{.dstSet = *h.set,
                             .dstBinding = 0,
                             .descriptorCount = 1,
                             .descriptorType =
                                 vk::DescriptorType::eCombinedImageSampler,
                             .pImageInfo = &imageInfo},
      {});
  return heights.emplace(texturePath, std::move(h)).first->second;
}
