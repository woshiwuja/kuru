#pragma once
#include "../../lib/plugin/plugin.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>

// WASD move on the ground plane, Q/E turn, middle mouse drag looks around,
// wheel dollies along the view direction. The speeds are feel, not physics:
// they are meant to be tuned.
struct Camera {
	glm::vec3 position = {2.0f, 2.0f, 2.0f};
	float     yaw      = -135.0f; // degrees, around +Y
	float     pitch    = -30.0f;

	float fov              = 45.0f; // degrees
	float nearPlane        = 0.1f;
	float farPlane         = 40.0f;
	float moveSpeed        = 3.0f;  // world units per second
	float turnSpeed        = 90.0f; // degrees per second, Q/E
	float lookSensitivity  = 0.15f; // degrees per pixel of mouse motion
	float zoomSpeed        = 0.5f;  // world units per wheel notch

	[[nodiscard]] glm::vec3 front() const;
};

// Owns the camera entity and turns raw input into a view and a projection.
// Register it before RenderPlugin: the renderer reads what this writes into
// the FrameContext.
struct CameraPlugin : public Plugin {
	void init(entt::registry &reg) override;
	void run(entt::registry &reg) override;
};
