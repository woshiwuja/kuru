#pragma once
#include <entt/entt.hpp>
#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace KR {

struct FrameContext {
	const vk::raii::CommandBuffer *commandBuffer = nullptr;
	uint32_t                       frameIndex    = 0;
	vk::Extent2D                   extent        = {};
	glm::mat4                      view          = glm::mat4(1.0f);
	glm::mat4                      proj          = glm::mat4(1.0f);
	// proj before the reversed-Z flip; sky_clouds.slang unprojects with it.
	glm::mat4                      skyRayProj    = glm::mat4(1.0f);
	bool                           uiCapturesMouse    = false;
	bool                           uiCapturesKeyboard = false;
};

// Reflects a component so the inspector can name, add and remove it without
// knowing the type. Registry members can't be attached directly: try_get and
// remove are overloaded or variadic, so &registry::foo<T> is ambiguous.
template<typename T>
void metaAdd(entt::registry &r, entt::entity e) { r.emplace_or_replace<T>(e); }
template<typename T>
void metaRemove(entt::registry &r, entt::entity e) { r.remove<T>(e); }

template<typename T>
void registerComponent() {
	using namespace entt::literals;
	entt::meta_factory<T>{}
		.template func<&metaAdd<T>>("add"_hs)
		.template func<&metaRemove<T>>("remove"_hs);
}

inline std::string typeName(const entt::meta_type &t) { return std::string{t.info().name()}; }

// A plugin is a set of components and systems. Concrete plugins live in
// src/game/plugins; this is only the interface Core runs them through.
//
// Core runs the three frame hooks as three passes over every plugin, not one
// pass calling all three: that is what lets a plugin open something in start()
// that the others use in update() and close it in end(). Within a pass the
// order is registration order.
struct Plugin {
	virtual ~Plugin() = default;

	virtual void init(entt::registry &reg) {}
	virtual void start(entt::registry &reg) {}
	virtual void update(entt::registry &reg) {}
	virtual void end(entt::registry &reg) {}
};
} // namespace KR
