#pragma once
#include "../../lib/plugin/plugin.hpp"
#include "plugins.hpp"
#include "render.hpp"
#include "mesh_registry.hpp"
#include "entt/entity/fwd.hpp"
#include "Recast.h"
#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/Shape/HeightFieldShape.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "../../lib/core/core.hpp"
#include "transform.hpp"

struct Map {};
struct MapPlugin : public Plugin {
    void init(entt::registry &reg)override{
        auto &p = Core::get()->physicsManager;
        // Render Transform and the Jolt heightfield have to use the same
        // factor, or you fall through / stand on invisible ground.
        constexpr float mapScale = 1000.0f;
        entt::entity mapEntity = reg.create();
		spawn(reg, mapEntity, "models/testmap.glb",
		      "textures/viking_room.ktx2", {0.0f, 0.0f, 1.0f, 0.0f},
		      Transform{.scale = glm::vec3(mapScale)});
		reg.emplace<Map>(mapEntity);
		reg.emplace<Sky>(reg.create());

		entt::resource<Mesh> mesh = getMesh(reg, "models/testmap.glb");

		constexpr uint32_t samples = 128;

		float minX = std::numeric_limits<float>::max();
		float maxX = std::numeric_limits<float>::lowest();
		float minZ = std::numeric_limits<float>::max();
		float maxZ = std::numeric_limits<float>::lowest();
		for (const Vertex &v : mesh->vertices) {
			minX = std::min(minX, v.pos.x);
			maxX = std::max(maxX, v.pos.x);
			minZ = std::min(minZ, v.pos.z);
			maxZ = std::max(maxZ, v.pos.z);
		}
		const float cellX = (maxX - minX) / (samples - 1);
		const float cellZ = (maxZ - minZ) / (samples - 1);

		// Filled by rasterizing each triangle's XZ footprint below; stays at
		// minY wherever no triangle covers a grid cell (holes at the border).
		std::vector<float> heights(samples * samples, mesh->minY);

		// No overhangs, so every grid cell has exactly one covering triangle -
		// project each triangle to grid space and barycentric-interpolate Y
		// for every cell inside it, rather than assuming vertices are already
		// laid out as a scanline grid (glTF export order isn't guaranteed to be).
		for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
			const glm::vec3 &a = mesh->vertices[mesh->indices[i]].pos;
			const glm::vec3 &b = mesh->vertices[mesh->indices[i + 1]].pos;
			const glm::vec3 &c = mesh->vertices[mesh->indices[i + 2]].pos;

			auto toGrid = [&](const glm::vec3 &p) {
				return glm::vec2((p.x - minX) / cellX, (p.z - minZ) / cellZ);
			};
			glm::vec2 ga = toGrid(a), gb = toGrid(b), gc = toGrid(c);

			int minGX = std::max(0, static_cast<int>(std::floor(std::min({ga.x, gb.x, gc.x}))));
			int maxGX = std::min(static_cast<int>(samples) - 1,
			                     static_cast<int>(std::ceil(std::max({ga.x, gb.x, gc.x}))));
			int minGZ = std::max(0, static_cast<int>(std::floor(std::min({ga.y, gb.y, gc.y}))));
			int maxGZ = std::min(static_cast<int>(samples) - 1,
			                     static_cast<int>(std::ceil(std::max({ga.y, gb.y, gc.y}))));

			const glm::vec2 v0 = gb - ga, v1 = gc - ga;
			const float den = v0.x * v1.y - v1.x * v0.y;
			if (std::abs(den) < 1e-8f) continue; // degenerate triangle

			for (int gz = minGZ; gz <= maxGZ; gz++) {
				for (int gx = minGX; gx <= maxGX; gx++) {
					const glm::vec2 v2 = glm::vec2(gx, gz) - ga;
					const float v = (v2.x * v1.y - v1.x * v2.y) / den;
					const float w = (v0.x * v2.y - v2.x * v0.y) / den;
					const float u = 1.0f - v - w;
					if (u < -1e-4f || v < -1e-4f || w < -1e-4f) continue; // outside
					heights[gz * samples + gx] = u * a.y + v * b.y + w * c.y;
				}
			}
		}

		JPH::HeightFieldShapeSettings settings(
		heights.data(),
		JPH::Vec3(minX * mapScale, 0.0f, minZ * mapScale),
		JPH::Vec3(cellX * mapScale, mapScale, cellZ * mapScale),
		samples
		);
		JPH::Shape::ShapeResult result = settings.Create();
		if (result.HasError())
		{
		throw("seh rotta");
		}
		JPH::ShapeRefC heightFieldShape = result.Get();
		// 2. Create the shape settings
		JPH::BodyCreationSettings bodySettings(
    heightFieldShape,
    JPH::RVec3(0, 0, 0),          // position
    JPH::Quat::sIdentity(),       // rotation
    JPH::EMotionType::Static,     // heightfields are always static
    kr::NON_MOVING            // your object layer for static geometry
		);
		JPH::BodyInterface &bodyInterface = p->bodies();
		JPH::BodyID heightFieldID = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::Activate);
    }
};
