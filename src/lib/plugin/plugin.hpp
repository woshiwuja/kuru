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
struct Plugin {
	virtual ~Plugin() = default;
	virtual void init(entt::registry &reg) {}
	virtual void run(entt::registry &reg) {}
};
