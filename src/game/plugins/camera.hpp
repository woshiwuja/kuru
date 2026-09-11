#pragma once
#include <Kuru.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

using namespace KR;

namespace KR{
    struct Camera {
	glm::vec3 pivot        = {0.0f, 0.0f, 0.0f};
	float     groundHeight = 2000.0f;

	float yaw      = -135.0f; // degrees, around +Y
	float pitch    = -30.0f;  // degrees, negative looks down at the pivot
	float distance = 5.0f;    // radius of the orbit, in world units

	float minDistance = 0.5f;
	float maxDistance = 5000.0f;
	float fov         = 45.0f;
	float nearPlane   = 0.1f;
	float farPlane    = 20000.0f;

	float panSpeed         = 1000.0f;  // world units per second
	float turnSpeed        = 90.0f; // degrees per second, Q/E
	float orbitSensitivity = 0.3f;  // degrees per pixel of mouse motion
	float zoomSpeed        = 0.1f;  // fraction of the radius per wheel notch

	[[nodiscard]] glm::vec3 front() const;
	[[nodiscard]] glm::vec3 position() const;

	void control(FrameContext &frame);
    };

    struct MainCamera {};

    struct CameraPlugin : public Plugin {
	void init(entt::registry &reg) override;
	void update(entt::registry &reg) override;
	void UI(entt::registry &reg);
    };
}
