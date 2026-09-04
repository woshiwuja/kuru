#pragma once
#include "../common/vertex.hpp"
#include <memory>
#include <string>
#include <vulkan/vulkan_raii.hpp>

// Each mesh owns its own buffers so meshes load and unload independently.
// The price is one bind pair per draw instead of one per frame; a shared arena
// would buy that back but needs suballocation and defrag to survive unload.
struct Mesh {
	vk::raii::Buffer       vertexBuffer       = nullptr;
	vk::raii::DeviceMemory vertexBufferMemory = nullptr;
	vk::raii::Buffer       indexBuffer        = nullptr;
	vk::raii::DeviceMemory indexBufferMemory  = nullptr;
	uint32_t               indexCount         = 0;
	// World height range of this mesh, fed to the terrain shading in the UBO.
	float minY = 0.0f;
	float maxY = 0.0f;

	void upload(const std::vector<Vertex> &vertices,
	            const std::vector<uint32_t> &indices);
};

std::shared_ptr<Mesh> loadModel(const std::string &path);
// Builds a grid mesh, one vertex per heightmap pixel. cellSize is world units
// between samples, heightScale the world height of a full-white pixel.
std::shared_ptr<Mesh> loadHeightfield(const std::string &path,
                                      float cellSize = 0.08f,
                                      float heightScale = 2.0f);
