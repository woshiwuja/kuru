#pragma once
#include <Kuru.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>

using namespace KR;

struct RenderPlugin;

// Matches OutlinePush in shaders/outline.slang.
struct OutlinePushConstants {
	glm::vec2 resolution;
	// See the shader: world-unit distance deltas rescaled to the threshold the
	// original Shadertoy pass was tuned against. Nudge to taste.
	float     distScale = 1.0f;
	float     strength  = .8f;
};

// A normal+distance prepass into graphics->normalImage before the main pass,
// then a fullscreen triangle inside it that subtracts the edges it finds.
// See assets/shaders/outline.slang.
//
// The prepass reuses RenderPlugin's mesh pipelineLayout and per-entity
// descriptor sets - it reads the same UBO at binding 0 and just ignores the
// texture - so only the pipeline itself is new. Register after RenderPlugin:
// init() needs it up, and update() has to run after the meshes are drawn.
struct OutlinePlugin : Plugin {
	RenderPlugin *render = nullptr;

	vk::raii::Pipeline            normalPipeline             = nullptr;
	vk::raii::DescriptorSetLayout outlineDescriptorSetLayout = nullptr;
	vk::raii::PipelineLayout      outlinePipelineLayout      = nullptr;
	vk::raii::Pipeline            outlinePipeline            = nullptr;
	vk::raii::DescriptorPool      outlineDescriptorPool      = nullptr;
	std::vector<vk::raii::DescriptorSet> outlineDescriptorSets;
	// One set is enough - it points at one image that never changes per frame -
	// but the view is recreated on resize, so track which one it was written for.
	vk::ImageView                 outlineBoundView           = nullptr;
	OutlinePushConstants          outlinePush;

	void init(entt::registry &reg) override;
	// Outside the main pass, unlike update().
	void start(entt::registry &reg) override;
	void update(entt::registry &reg) override;

	void createNormalPipeline();
	void createOutlineDescriptorSetLayout();
	void createOutlinePipeline();
	void createOutlineDescriptorSet();

	void drawNormalPrepass(entt::registry &reg);
	void drawOutline(entt::registry &reg);
};
