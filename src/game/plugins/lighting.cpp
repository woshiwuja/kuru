#include "lighting.hpp"

using namespace KR;

void LightingPlugin::init(entt::registry &reg) {
	reg.emplace<DirectionalLight>(reg.create());
}
