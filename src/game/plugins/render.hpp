#pragma once
#include "../../lib/image/image.hpp"
#include "../../lib/model/model.hpp"
#include "../../lib/plugin/plugin.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vulkan/vulkan_raii.hpp>

// ---- components -------------------------------------------------------------

struct Transform {
	glm::vec3 position = {0.0f, 0.0f, 0.0f};
	glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
	glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
	glm::mat4 matrix() const;
};

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
	std::vector<vk::raii::DescriptorSet> descriptorSets;

	// std::vector<move-only> still reports as copy-constructible, so entt picks
	// its copy path and only fails deep inside the instantiation. Say it here.
	Renderable()                              = default;
	Renderable(const Renderable &)            = delete;
	Renderable &operator=(const Renderable &) = delete;
	Renderable(Renderable &&)                 = default;
	Renderable &operator=(Renderable &&)      = default;
};

// ---- plugin -----------------------------------------------------------------

struct RenderPlugin : Plugin {
	// The pipeline, its layout and the descriptor pool describe how this plugin
	// draws: they belong to it, not to Core.
	vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
	vk::raii::PipelineLayout      pipelineLayout      = nullptr;
	vk::raii::Pipeline            graphicsPipeline    = nullptr;
	vk::raii::DescriptorPool      descriptorPool      = nullptr;

	void init(entt::registry &reg) override;
	void update(entt::registry &reg) override;

	void createDescriptorSetLayout();
	void createGraphicsPipeline();
	void createDescriptorPool();
	[[nodiscard]] vk::raii::ShaderModule
	createShaderModule(const std::vector<char> &code) const;

	// Gives an entity that already has a MeshRef the GPU state to be drawn.
	// Emplaced up front rather than through an on_construct hook: the hook would
	// depend on MaterialRef being added before MeshRef.
	void attach(entt::registry &reg, entt::entity entity,
	            std::shared_ptr<Texture> texture, glm::vec4 params);
	entt::entity spawn(entt::registry &reg, std::shared_ptr<Mesh> mesh,
	                   std::shared_ptr<Texture> texture, glm::vec4 params,
	                   Transform transform = {});
	void despawn(entt::registry &reg, entt::entity entity);

	// systems
	void updateUniforms(entt::registry &reg);
	void drawMeshes(entt::registry &reg);
};
