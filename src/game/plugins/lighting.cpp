#include "lighting.hpp"

void LightingPlugin::init(entt::registry &reg) {
	reg.emplace<DirectionalLight>(reg.create());
}
