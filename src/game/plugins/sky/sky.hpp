#pragma once
#include <Kuru.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

using namespace KR;

// One entity carrying this drives the sky; SkyPlugin::init creates it.
struct Sky {
	glm::vec4 color{1.0f}; // sky dome tint; see skyTint in sky_clouds.slang
	// Was the first DirectionalLight's color: now set it here, the procedural
	// sky only ever models the one sun.
	glm::vec4 sunColor{1.0f};
};

struct SkyUniformBufferObject {
	glm::vec2 resolution;
	float     time;
	float     _pad = 0.0f;
	glm::mat4 invViewRotProj;
	glm::vec4 sunColor;
	glm::vec4 skyTint;
};

struct SkyPlugin : Plugin {
	vk::raii::DescriptorSetLayout skyDescriptorSetLayout = nullptr;
	vk::raii::PipelineLayout      skyPipelineLayout      = nullptr;
	vk::raii::Pipeline            skyPipeline            = nullptr;
	vk::raii::DescriptorPool      skyDescriptorPool      = nullptr;
	std::shared_ptr<Texture> skyNoiseTexture;
	std::vector<vk::raii::Buffer>        skyUniformBuffers;
	std::vector<vk::raii::DeviceMemory>  skyUniformBuffersMemory;
	std::vector<void *>                  skyUniformBuffersMapped;
	std::vector<vk::raii::DescriptorSet> skyDescriptorSets;
	float skyTime = 0.0f; // iTime: seconds since startup, accumulated from deltaTime

	void init(entt::registry &reg) override;
	void update(entt::registry &reg) override;

	void createSkyDescriptorSetLayout();
	void createSkyPipeline();
	void createSkyResources(); // noise texture, per-frame UBOs, descriptor sets

	void updateSkyUniforms(entt::registry &reg);
	void drawSky(entt::registry &reg);
};
