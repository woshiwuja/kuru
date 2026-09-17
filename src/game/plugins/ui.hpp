#pragma once
#include <Kuru.h>
#include <entt/entt.hpp>

using namespace KR;

struct UIPlugin : public Plugin {
	void init(entt::registry &reg) override;
	void start(entt::registry &reg) override;
	void update(entt::registry &reg) override;
	void end(entt::registry &reg) override;
};
