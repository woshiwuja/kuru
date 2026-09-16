#pragma once
#include <Kuru.h>
#include <entt/entt.hpp>

using namespace KR;

struct UiPlugin : public Plugin {
	~UiPlugin() override;
	void init(entt::registry &reg) override;
	void start(entt::registry &reg) override;
	void update(entt::registry &reg) override;
	void end(entt::registry &reg) override;
};
