#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

// What Core hands the plugins for the frame in flight. Lives in reg.ctx(), so
// Plugin::run keeps the plain registry signature.
struct FrameContext {
	const vk::raii::CommandBuffer *commandBuffer = nullptr;
	uint32_t                       frameIndex    = 0;
	vk::Extent2D                   extent        = {};
	glm::mat4                      view          = glm::mat4(1.0f);
	glm::mat4                      proj          = glm::mat4(1.0f);
	// Set by whatever plugin draws the UI, read by whoever consumes input. It
	// lags a frame for plugins that run before the UI, which nobody can see.
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
	// Before the render pass opens: uploads, spawning, opening a UI frame.
	virtual void start(entt::registry &reg) {}
	// Inside the render pass: the drawing.
	virtual void update(entt::registry &reg) {}
	// Still inside the render pass, after every update: whatever has to go last.
	virtual void end(entt::registry &reg) {}
};
