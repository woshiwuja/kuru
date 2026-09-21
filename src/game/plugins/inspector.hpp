#pragma once
#include "default.hpp" // Selected
#include <Kuru.h>

using namespace KR;
using namespace entt::literals;

struct InspectorPlugin : public Plugin {
	void update(entt::registry &r) override { UI(r); }

	void UI(entt::registry &r) {
		using namespace ImGui;
		if (Begin("Inspector")) {
			auto selected = r.view<Selected>();
			auto current = (selected.begin() == selected.end()) ? entt::null : *selected.begin();

			if (BeginChild("entities", {GetContentRegionAvail().x * 0.5f, 0},
			               ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
				tree(r, current);
			}
			EndChild();
			SameLine();
			if (BeginChild("components", {0, 0}, ImGuiChildFlags_Borders)) {
				if (current == entt::null) {
					TextDisabled("no entity selected");
				} else {
					components(r, current);
				}
			}
			EndChild();
		}
		End();
	}

	void tree(entt::registry &r, entt::entity current) {
		using namespace ImGui;
		if (!TreeNodeEx("entities", ImGuiTreeNodeFlags_DefaultOpen)) return;
		for (auto e : r.view<entt::entity>()) {
			auto flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
			             ImGuiTreeNodeFlags_SpanAvailWidth;
			if (e == current) flags |= ImGuiTreeNodeFlags_Selected;
			TreeNodeEx(reinterpret_cast<void *>(static_cast<uintptr_t>(entt::to_integral(e))),
			           flags, "%s", entityName(e).c_str());
			if (IsItemClicked()) {
				r.clear<Selected>();
				r.emplace<Selected>(e);
			}
		}
		TreePop();
	}

	static std::string entityName(entt::entity e) {
		return "entity " + std::to_string(entt::to_integral(e));
	}

	void components(entt::registry &r, entt::entity e) {
		using namespace ImGui;
		Text("%s", entityName(e).c_str());
		Separator();
		for (auto [id, storage] : r.storage()) {
			auto type = entt::resolve(storage.type());
			if (!type || !storage.contains(e)) continue;
			PushID(static_cast<int>(id));
			Text("%s", typeName(type).c_str());
			SameLine();
			if (Button("remove")) type.invoke("remove"_hs, {}, entt::forward_as_meta(r), e);
			PopID();
		}
		Separator();
		if (Button("Add component")) OpenPopup("add component");
		if (BeginPopup("add component")) {
			for (auto [id, type] : entt::resolve()) {
				const auto *storage = r.storage(id);
				if (storage != nullptr && storage->contains(e)) continue;
				if (Selectable(typeName(type).c_str())) {
					type.invoke("add"_hs, {}, entt::forward_as_meta(r), e);
					CloseCurrentPopup();
				}
			}
			EndPopup();
		}
	}
};
