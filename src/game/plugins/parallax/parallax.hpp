#pragma once
#include <Kuru.h>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan_raii.hpp>

using namespace KR;

struct RenderPlugin;

// Matches ParallaxPush in assets/shaders/parallax.slang. The height comes from
// "<texture>_height.png" next to the entity's texture (Model::texture), white
// = high; without one, the texture's own brightness stands in.
struct Parallax {
	float    depth      = 0.02f;  // relief depth, in metres
	uint32_t steps      = 32;     // ray-march layers at grazing angles
	uint32_t invert     = 0;      // 1: dark = raised
	float    bump       = 1.0f;   // how much the height bends the lighting normal
	float    shadow     = 8.0f;   // self-shadow strength, 0 = off
	float    fadeDist   = 30.0f;  // metres: flat shading past this
	uint32_t silhouette = 1;      // discard where the relief leaves uv [0, 1]
};

// Draws every Parallax entity with parallax.slang instead of the main pass
// (tags it CustomDraw, which RenderPlugin skips). Reuses RenderPlugin's
// descriptor sets as set 0 and binds the height map as set 1.
// Register after RenderPlugin: init() needs it, update() draws in its pass.
struct ParallaxPlugin : Plugin {
	ParallaxPlugin() { registerComponent<Parallax>(); }

	RenderPlugin                 *render          = nullptr;
	vk::raii::DescriptorSetLayout heightSetLayout = nullptr;
	vk::raii::PipelineLayout      pipelineLayout  = nullptr;
	vk::raii::Pipeline            pipeline        = nullptr;
	vk::raii::DescriptorPool      heightPool      = nullptr;
	// One set per texture path, made on first use. After heightPool, so the
	// sets go first.
	struct Height {
		std::shared_ptr<Texture> texture;
		vk::raii::DescriptorSet  set = nullptr;
	};
	std::unordered_map<std::string, Height> heights;

	void init(entt::registry &reg) override;
	void update(entt::registry &reg) override;
	const Height &height(entt::registry &reg, const std::string &texturePath);
};
