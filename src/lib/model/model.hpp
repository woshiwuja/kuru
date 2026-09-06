#pragma once
#include "../common/vertex.hpp"
#include "../image/image.hpp"
#include <memory>
#include <string>
#include <vulkan/vulkan_raii.hpp>

// One glTF primitive's slice of the mesh's shared index buffer, plus its own
// material texture (null if the primitive had none - falls back to whatever
// texture the entity was spawned with).
struct SubMesh {
	uint32_t indexOffset = 0;
	uint32_t indexCount  = 0;
	std::shared_ptr<Texture> texture;
};

// Each mesh owns its own buffers so meshes load and unload independently.
// The price is one bind pair per draw instead of one per frame; a shared arena
// would buy that back but needs suballocation and defrag to survive unload.
struct Mesh {
	vk::raii::Buffer       vertexBuffer       = nullptr;
	vk::raii::DeviceMemory vertexBufferMemory = nullptr;
	vk::raii::Buffer       indexBuffer        = nullptr;
	vk::raii::DeviceMemory indexBufferMemory  = nullptr;
	uint32_t               indexCount         = 0;
	float minY = 0.0f;
	float maxY = 0.0f;
	// One entry per glTF primitive: a multi-material model (e.g. a character
	// with separate body/claws/head/legs materials) needs one draw call and
	// one texture per primitive, not one texture stretched over everything.
	std::vector<SubMesh> submeshes;

	void upload(const std::vector<Vertex> &vertices,
	            const std::vector<uint32_t> &indices);
};

std::shared_ptr<Mesh> loadModel(const std::string &path);
