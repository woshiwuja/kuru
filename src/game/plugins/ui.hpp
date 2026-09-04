#pragma once
#include "../../lib/plugin/plugin.hpp"
#include <entt/entt.hpp>

// Dear ImGui, docking branch. Register it last: it draws over everything else
// and it is the one that tells the other plugins when the UI owns the input.
//
// It opens the ImGui frame in start() and submits it in end(), so any plugin
// can call ImGui:: freely from its own update() and have the windows land in
// this frame.
struct UiPlugin : public Plugin {
	~UiPlugin() override;
	void init(entt::registry &reg) override;
	void start(entt::registry &reg) override;
	void update(entt::registry &reg) override;
	void end(entt::registry &reg) override;

private:
	bool started = false;
};
