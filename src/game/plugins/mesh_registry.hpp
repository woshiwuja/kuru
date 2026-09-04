#pragma once

#include <entt/entt.hpp>
#include <memory>
#include <string>

#include "../../lib/common/common.hpp"
#include "../../lib/model/model.hpp"
#include "../../lib/plugin/plugin.hpp"

struct MeshLoader
{
	using result_type = std::shared_ptr<Mesh>;

	result_type operator()(const std::string &path) const
	{
		const bool isGltf = path.ends_with(".glb") || path.ends_with(".gltf");
		return isGltf ? loadModel(path) : loadHeightfield(path);
	}

	result_type operator()(const std::string &path, float cellSize, float heightScale) const
	{
		return loadHeightfield(path, cellSize, heightScale);
	}
};

using MeshCache = entt::resource_cache<Mesh, MeshLoader>;

// The cache lives in the registry's context, so anything holding the registry reaches it
// without another global.
struct MeshRegistryPlugin : public Plugin
{
	void init(entt::registry &reg) override
	{
		reg.ctx().emplace<MeshCache>();
	}
};

// ponytail: the cache is keyed by path only, so the same file loaded twice with
// different tuning returns the first mesh built. Key on the args if that bites.
template <typename... Args>
entt::resource<Mesh> getMesh(entt::registry &reg, const std::string &path, Args &&...args)
{
	auto &cache = reg.ctx().get<MeshCache>();
	return cache
	    .load(entt::hashed_string::value(path.c_str()), path, std::forward<Args>(args)...)
	    .first->second;
}
