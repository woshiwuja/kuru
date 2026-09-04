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
struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	static vk::VertexInputBindingDescription getBindingDescription()
	{
		return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
	}
	static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
	{
		return {
		    vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
		    vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
		    vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))};
	}

	bool operator==(const Vertex &other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}
};

template <>
struct std::hash<Vertex>
{
	size_t operator()(Vertex const &vertex) const noexcept
	{
		return ((std::hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
	}
};

struct UniformBufferObject
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
	// x: 1 = procedural terrain shading, 0 = plain textured mesh. y, z: world height range.
	alignas(16) glm::vec4 material;
};
