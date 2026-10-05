#pragma once
#include <Kuru.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
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
	// Shadow map for dirLights[0]: world -> light clip space, and
	// x: map valid this frame, y: texel size, z: depth bias.
	alignas(16) glm::mat4 lightViewProj{1.0f};
	alignas(16) glm::vec4 shadow{0.0f};
	// Spot i = layer i of the local array, point i face f = layer
	// MAX_LIGHTS + 6i + f. localShadow: x/y = spot/point lights rendered,
	// z = texel size, w = depth bias.
	alignas(16) glm::mat4 spotViewProj[MAX_LIGHTS];
	alignas(16) glm::mat4 pointViewProj[MAX_LIGHTS * 6];
	alignas(16) glm::vec4 localShadow{0.0f};
};
// The C++ <-> slang correspondence is maintained by hand, so pin the one
// number that catches a drift in either file.
static_assert(sizeof(UniformBufferObject) ==
                  3 * 64 + 4 * 16 + MAX_LIGHTS * (32 + 32 + 48) + 16 +
                      64 + 16 + MAX_LIGHTS * 7 * 64 + 16,
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

// What a path-based spawn()/spawnProp() was made from. Plain data, so it is
// saved, and RenderPlugin::load re-spawns MeshRef/MaterialRef/Renderable from
// it. Entities spawned from an in-memory mesh have none and aren't restored.
struct Model {
	// ponytail: fixed buffers keep it trivially copyable for SaveFile; give
	// SaveFile string support if paths ever outgrow 127 chars.
	char mesh[128] = "";
	char texture[128] = "";
	glm::vec4 params{0.0f};
	bool prop = false;
};

struct DebugMesh {};
struct DebugWire {};
struct Prop {};

struct PropBatch {
	std::shared_ptr<Mesh>     mesh;
	std::shared_ptr<Material> material;
	Renderable                renderable; // shared UBO: view/proj/lights/material
	struct InstanceBuffer {
		vk::raii::Buffer       buffer   = nullptr;
		vk::raii::DeviceMemory memory   = nullptr;
		glm::mat4             *mapped   = nullptr;
		uint32_t               capacity = 0;
	};
	std::array<InstanceBuffer, MAX_FRAMES_IN_FLIGHT> instances; // vertex binding 1
	uint32_t                                         count = 0;
};

struct RenderPlugin : Plugin {
	RenderPlugin() {
		registerComponent<Renderable>();
		registerComponent<MeshRef>();
		registerComponent<Prop>();
		registerComponent<DebugMesh>();
		registerComponent<DebugWire>();
		registerComponent<Model>();
	}
	vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
	vk::raii::PipelineLayout      pipelineLayout      = nullptr;
	vk::raii::Pipeline            graphicsPipeline    = nullptr;
	vk::raii::Pipeline            debugPipeline       = nullptr;
	vk::raii::Pipeline            debugWirePipeline   = nullptr;
	vk::raii::Pipeline            propPipeline        = nullptr;
	vk::raii::DescriptorPool      descriptorPool      = nullptr;

	// Directional shadow map: depth from dirLights[0], drawn in start() and
	// sampled by every mesh at binding 2. One image is enough - the barrier in
	// drawShadowPass waits for the previous frame's reads before rewriting it.
	static constexpr uint32_t   shadowSize   = 2048;
	static constexpr vk::Format shadowFormat = vk::Format::eD32Sfloat;
	vk::raii::Image             shadowImage  = nullptr;
	vk::raii::DeviceMemory      shadowMemory = nullptr;
	vk::raii::ImageView         shadowView   = nullptr;
	vk::raii::Sampler           shadowSampler  = nullptr;
	vk::raii::Pipeline          shadowPipeline = nullptr;
	// Spot and point lights share one layered map: spots first, then six
	// faces per point light (layer layout in UniformBufferObject).
	static constexpr uint32_t localShadowSize   = 512;
	static constexpr uint32_t localShadowLayers = MAX_LIGHTS * 7;
	vk::raii::Image                  localShadowImage     = nullptr;
	vk::raii::DeviceMemory           localShadowMemory    = nullptr;
	vk::raii::ImageView              localShadowArrayView = nullptr; // sampled
	std::vector<vk::raii::ImageView> localShadowLayerViews;          // rendered
	// Computed in start() for the passes, copied into every UBO in update(),
	// so the shader always compares against the matrices the maps used.
	bool                                     sunShadowValid = false;
	glm::mat4                                sunViewProj{1.0f};
	std::array<glm::mat4, MAX_LIGHTS>        spotViewProj{};
	std::array<glm::mat4, MAX_LIGHTS * 6>    pointViewProj{};
	uint32_t                                 spotShadowCount  = 0;
	uint32_t                                 pointShadowCount = 0;
	struct ShadowConfig {
		bool  enabled     = true;
		float halfExtent  = 30.0f;  // box half-width around the camera, metres
		float depthRange  = 200.0f; // box half-depth along the light
		float biasConst   = 1.25f;  // rasterizer depth bias, in depth units
		float biasSlope   = 1.75f;  // ...scaled by the triangle's slope
		float shaderBias  = 0.0005f;
		bool  localEnabled    = true;   // spot + point lights
		float spotFar         = 50.0f;  // spots have no range, so the map needs one
		float localShaderBias = 0.0002f;
	} shadowConfig;

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
	std::unordered_map<const Mesh *, PropBatch> propBatches;

	void init(entt::registry &reg) override;
	void start(entt::registry &reg) override;
	void update(entt::registry &reg) override;
	void save(entt::registry &reg, SaveFile &file) override;
	void load(entt::registry &reg, SaveFile &file) override;

	void createDescriptorSetLayout();
	void createGraphicsPipeline();
	void createDescriptorPool();
	void createShadowResources();
	void drawShadowPass(entt::registry &reg);
	// lightViewProj for dirLights[0], centred on `center`; false if no light.
	bool shadowMatrix(entt::registry &reg, glm::vec3 center, glm::mat4 &out) const;
	void updateShadowMatrices(entt::registry &reg);
	// viewProj == nullptr: clear only.
	void shadowLayerPass(entt::registry &reg, vk::ImageView view, uint32_t size,
	                     const glm::mat4 *viewProj);
	[[nodiscard]] vk::raii::ShaderModule
	createShaderModule(const std::vector<char> &code) const;

	void attach(entt::registry &reg, entt::entity entity,
	            std::shared_ptr<Texture> texture, glm::vec4 params);
	void spawn(entt::registry &reg, entt::entity entity,
	           std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture> texture,
	           glm::vec4 params, Transform transform = {});
	void spawnProp(entt::registry &reg, entt::entity entity,
	               std::shared_ptr<Mesh> mesh, std::shared_ptr<Texture> texture,
	               Transform transform = {});
	void despawn(entt::registry &reg, entt::entity entity);

	[[nodiscard]] Renderable makeRenderable(const Mesh &mesh,
	                                        const std::shared_ptr<Texture> &texture);
	void updateUniforms(entt::registry &reg);
	void drawMeshes(entt::registry &reg);
};

void drawRenderable(const vk::raii::CommandBuffer &commandBuffer,
                    const vk::raii::PipelineLayout &layout, uint32_t frameIndex,
                    const Mesh &mesh, const Renderable &renderable,
                    uint32_t instanceCount = 1);

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
void spawnProp(entt::registry &reg, entt::entity entity,
               const std::string &meshPath, const std::string &texturePath,
               Transform transform = {});
