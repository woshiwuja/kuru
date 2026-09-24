#pragma once

// std::hash<glm::vec2/vec3> lives in glm/gtx/hash.hpp, so it has to be included here,
// before the std::hash<Vertex> specialization below instantiates it. Relying on the
// includer to have pulled it in first gives "partial specialization after instantiation".
#define GLM_ENABLE_EXPERIMENTAL        // required by glm/gtx/hash.hpp
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <array>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <entt/entt.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <functional>

namespace KR {
struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
	glm::vec3 normal;

	static vk::VertexInputBindingDescription getBindingDescription()
	{
		return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
	}
	static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions()
	{
		return {
		    vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
		    vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
		    vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord)),
		    vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal))};
	}

	bool operator==(const Vertex &other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord &&
		       normal == other.normal;
	}
};

} // namespace KR

template <>
struct std::hash<KR::Vertex>
{
	size_t operator()(KR::Vertex const &vertex) const noexcept
	{
		return ((std::hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
		       (hash<glm::vec2>()(vertex.texCoord) << 1) ^ (hash<glm::vec3>()(vertex.normal) << 1);
	}
};

namespace KR {
// Must match MAX_LIGHTS in assets/shaders/slang.slang.
constexpr uint32_t MAX_LIGHTS = 4;

// One LightingPlugin DirectionalLight, as it's packed into the UBO's light
// array. xyz used in both fields, w is padding to keep each light a clean
// 32-byte, 16-byte-aligned pair for the shader's cbuffer array.
struct GPULight
{
	alignas(16) glm::vec4 direction;
	alignas(16) glm::vec4 color;
};

struct UniformBufferObject
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
	// x: 2 = debug overlay (flat vertex color, y = its alpha), 0 = textured mesh.
	alignas(16) glm::vec4 material;
	alignas(16) GPULight lights[MAX_LIGHTS];
	uint32_t lightCount = 0;
};
} // namespace KR
