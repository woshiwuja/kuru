#pragma once
#include <Kuru.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vulkan/vulkan_raii.hpp>
#include "../lighting/lighting.hpp"
#include "../transform/transform.hpp"

using namespace KR;

struct MeshRef {
	std::shared_ptr<Mesh> mesh;
};

struct UniformBufferObject
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
	alignas(16) glm::vec4 material;
	alignas(16) glm::vec4 baseColor{1.0f};
	alignas(16) glm::vec4 pbr{1.0f, 1.0f, 0.0f, 0.0f};
	alignas(16) glm::vec4 cameraPos{0.0f};
	alignas(16) DirectionalLight dirLights[MAX_LIGHTS];
	alignas(16) PointLight       pointLights[MAX_LIGHTS];
	alignas(16) SpotLight        spotLights[MAX_LIGHTS];
	// x: directional, y: point, z: spot. w unused. One uvec4 rather than three
	// trailing uints, so there is no scalar-offset question at the end.
	alignas(16) glm::uvec4 counts{0};
};
// The C++ <-> slang correspondence is maintained by hand, so pin the one
// number that catches a drift in either file.
static_assert(sizeof(UniformBufferObject) ==
                  3 * 64 + 4 * 16 + MAX_LIGHTS * (32 + 32 + 48) + 16,
              "UniformBufferObject no longer matches its std140 layout");

struct Renderable {
	std::vector<vk::raii::Buffer>        uniformBuffers;
	std::vector<vk::raii::DeviceMemory>  uniformBuffersMemory;
	std::vector<void *>                  uniformBuffersMapped;
	std::vector<std::vector<vk::raii::DescriptorSet>> descriptorSets;
	Renderable()                              = default;
	Renderable(const Renderable &)            = delete;
	Renderable &operator=(const Renderable &) = delete;
	Renderable(Renderable &&)                 = default;
	Renderable &operator=(Renderable &&)      = default;
};

struct DebugMesh {};

struct RenderPlugin : Plugin {
	vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
	vk::raii::PipelineLayout      pipelineLayout      = nullptr;
	vk::raii::Pipeline            graphicsPipeline    = nullptr;
	vk::raii::Pipeline            debugPipeline       = nullptr;
	vk::raii::DescriptorPool      descriptorPool      = nullptr;

	// GPU resources of despawned entities, kept until every frame that could
	// reference them has retired. Declared after descriptorPool so the sets go
	// first on destruction.
	struct Retired {
		uint64_t                  frame = 0;
		Renderable                renderable;
		std::shared_ptr<Mesh>     mesh;
		std::shared_ptr<Material> material;
	};
	std::vector<Retired> retired;
	uint64_t             frameCount = 0;

	void init(entt::registry &reg) override;
	void start(entt::registry &reg) override;
	void update(entt::registry &reg) override;

	void createDescriptorSetLayout();
	void createGraphicsPipeline();
	void createDescriptorPool();
	[[nodiscard]] vk::raii::ShaderModule
	createShaderModule(const std::vector<char> &code) const;

	void attach(entt::registry &reg, entt::entity entity,
	            std::shared_ptr<Texture> texture, glm::vec4 params);
	void spawn(entt::registry &reg, entt::entity entity,
	           std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture> texture,
	           glm::vec4 params, Transform transform = {});
	void despawn(entt::registry &reg, entt::entity entity);

	void updateUniforms(entt::registry &reg);
	void drawMeshes(entt::registry &reg);
};

void drawRenderable(const vk::raii::CommandBuffer &commandBuffer,
                    const vk::raii::PipelineLayout &layout, uint32_t frameIndex,
                    const Mesh &mesh, const Renderable &renderable);

inline RenderPlugin &renderer(entt::registry &reg) {
  auto *plugin = reg.ctx().find<RenderPlugin *>();
  assert(plugin != nullptr && *plugin != nullptr &&
         "RenderPlugin must be registered and initialised first");
  return **plugin;
}

std::shared_ptr<Texture> getTexture(entt::registry &reg,
                                    const std::string &path);

void spawn(entt::registry &reg, entt::entity entity,
           const std::string &meshPath, const std::string &texturePath,
           glm::vec4 params = {}, Transform transform = {});
