#pragma once
#include "../../lib/plugin/plugin.hpp"
#include <entt/entt.hpp>

// Dear ImGui, docking branch. Register it last: it draws over everything else
// and it is the one that tells the other plugins when the UI owns the input.
//
// ponytail: this plugin opens the frame, builds every panel and closes it. When
// another plugin needs to add windows, split run() into begin/end and give them
// a slot in between.
struct UiPlugin : public Plugin {
	~UiPlugin() override;
	void init(entt::registry &reg) override;
	void run(entt::registry &reg) override;

private:
	bool started = false;
	void drawPanels(entt::registry &reg);
};
