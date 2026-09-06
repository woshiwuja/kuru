#pragma once
#include "../../lib/image/image.hpp"
#include "../../lib/model/model.hpp"
#include "../../lib/plugin/plugin.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vulkan/vulkan_raii.hpp>
#include "transform.hpp"

// ---- components -------------------------------------------------------------


// Handles, not data: several entities share one asset and it dies with the last
// of them.
struct MeshRef {
	std::shared_ptr<Mesh> mesh;
};

struct MaterialRef {
	std::shared_ptr<Texture> texture;
	// x: 1 = procedural terrain shading, 0 = plain textured mesh. y, z: world height range.
	glm::vec4 params = {0.0f, 0.0f, 1.0f, 0.0f};
};

// Per-entity GPU binding state, one slot per frame in flight. Derived from
// MeshRef + MaterialRef, owned by this plugin: keeping descriptor sets out of
// MaterialRef is what lets two entities share a material.
struct Renderable {
	std::vector<vk::raii::Buffer>        uniformBuffers;
	std::vector<vk::raii::DeviceMemory>  uniformBuffersMemory;
	std::vector<void *>                  uniformBuffersMapped;
	// Outer index: submesh (one per glTF primitive, at least one entry even for
	// a single-material mesh). Inner index: frame in flight. All submeshes of
	// one entity share the same uniform buffers above - only the bound texture
	// (and thus the descriptor set) differs between them.
	std::vector<std::vector<vk::raii::DescriptorSet>> descriptorSets;

	// std::vector<move-only> still reports as copy-constructible, so entt picks
	// its copy path and only fails deep inside the instantiation. Say it here.
	Renderable()                              = default;
	Renderable(const Renderable &)            = delete;
	Renderable &operator=(const Renderable &) = delete;
	Renderable(Renderable &&)                 = default;
	Renderable &operator=(Renderable &&)      = default;
};

// Marker: at most one entity should carry this. RenderPlugin draws it first,
// full-screen, with depth write off, so meshes draw over it wherever they
// exist. No fields - shaders/sky_clouds.slang reads only time and viewport size.
struct Sky {};

// ---- plugin -----------------------------------------------------------------

// Matches ShaderConstants in shaders/sky_clouds.slang. cbuffer packing keeps
// this whole thing in one 16-byte register regardless of the trailing pad.
struct SkyUniformBufferObject {
	glm::vec2 resolution;
	float     time;
	float     _pad = 0.0f;
	// inverse(proj * rotation-only view): reconstructs a per-pixel world-space
	// ray direction that tracks camera orientation but not position, so the sky
	// rotates correctly with the camera while staying put as the camera moves.
	glm::mat4 invViewRotProj;
	// First DirectionalLight's color, so the sky tints with the sun instead of
	// the old fixed dither noise. w unused.
	glm::vec4 sunColor;
};

struct RenderPlugin : Plugin {
	// The pipeline, its layout and the descriptor pool describe how this plugin
	// draws: they belong to it, not to Core.
	vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
	vk::raii::PipelineLayout      pipelineLayout      = nullptr;
	vk::raii::Pipeline            graphicsPipeline    = nullptr;
	vk::raii::DescriptorPool      descriptorPool      = nullptr;

	// Sky: a separate pipeline (no vertex input, depth write off) and its own
	// small descriptor pool, since its binding layout (UBO + split
	// texture/sampler, no vertex buffer) doesn't match the mesh pipeline's.
	vk::raii::DescriptorSetLayout skyDescriptorSetLayout = nullptr;
	vk::raii::PipelineLayout      skyPipelineLayout       = nullptr;
	vk::raii::Pipeline            skyPipeline              = nullptr;
	vk::raii::DescriptorPool      skyDescriptorPool        = nullptr;
	// Procedurally generated: the port needs a Shadertoy-style RGBA noise
	// texture and we have no asset for one, so RenderPlugin builds it once.
	std::shared_ptr<Texture> skyNoiseTexture;
	std::vector<vk::raii::Buffer>        skyUniformBuffers;
	std::vector<vk::raii::DeviceMemory>  skyUniformBuffersMemory;
	std::vector<void *>                  skyUniformBuffersMapped;
	std::vector<vk::raii::DescriptorSet> skyDescriptorSets;
	float skyTime = 0.0f; // iTime: seconds since startup, accumulated from deltaTime

	void init(entt::registry &reg) override;
	void update(entt::registry &reg) override;

	void createDescriptorSetLayout();
	void createGraphicsPipeline();
	void createDescriptorPool();
	[[nodiscard]] vk::raii::ShaderModule
	createShaderModule(const std::vector<char> &code) const;

	void createSkyDescriptorSetLayout();
	void createSkyPipeline();
	void createSkyResources(); // noise texture, per-frame UBOs, descriptor sets

	void attach(entt::registry &reg, entt::entity entity,
	            std::shared_ptr<Texture> texture, glm::vec4 params);
	// Attaches mesh + texture to an entity the caller already created, rather
	// than creating one of its own - that's what let DefaultPlugin's mesh end
	// up on a different entity than its Character/Name/Transform. Transform is
	// only emplaced if `entity` doesn't already carry one.
	void spawn(entt::registry &reg, entt::entity entity,
	           std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture> texture,
	           glm::vec4 params, Transform transform = {});
	void despawn(entt::registry &reg, entt::entity entity);

	// systems
	void updateUniforms(entt::registry &reg);
	void drawMeshes(entt::registry &reg);
	void updateSkyUniforms(entt::registry &reg);
	void drawSky(entt::registry &reg);
};
