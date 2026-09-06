#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

struct FrameContext {
	const vk::raii::CommandBuffer *commandBuffer = nullptr;
	uint32_t                       frameIndex    = 0;
	vk::Extent2D                   extent        = {};
	glm::mat4                      view          = glm::mat4(1.0f);
	glm::mat4                      proj          = glm::mat4(1.0f);
	bool                           uiCapturesMouse    = false;
	bool                           uiCapturesKeyboard = false;
};

// A plugin is a set of components and systems. Concrete plugins live in
// src/game/plugins; this is only the interface Core runs them through.
//
// Core runs the three frame hooks as three passes over every plugin, not one
// pass calling all three: that is what lets a plugin open something in start()
// that the others use in update() and close it in end(). Within a pass the
// order is registration order.
struct Plugin {
	virtual ~Plugin() = default;

	// Once, after the device exists.
	virtual void init(entt::registry &reg) {}
	virtual void start(entt::registry &reg) {}
	virtual void update(entt::registry &reg) {}
	virtual void end(entt::registry &reg) {}
};
