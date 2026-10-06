#pragma once
#include "imgui.h"
#include <Kuru.h>
#include <algorithm>
#include <vector>

using namespace KR;
using namespace entt::literals;

struct InspectorPlugin : public Plugin {
  InspectorPlugin() { registerComponent<Selected>(); }
  entt::entity anchor = entt::null; // where a shift-click range starts
  void update(entt::registry &r) override { UI(r); }

  void UI(entt::registry &r) {
    using namespace ImGui;
    const auto *core = Core::get();
    if (Begin("Inspector")) {
      auto selected = r.view<Selected>();
      auto current =
          (selected.begin() == selected.end()) ? entt::null : *selected.begin();

      // Leave one button row under the panels, or the buttons scroll away.
      const float panelHeight = -GetFrameHeightWithSpacing();
      if (BeginChild("entities", {GetContentRegionAvail().x * 0.5f, panelHeight},
                     ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        tree(r);
      }
      EndChild();
      SameLine();
      if (BeginChild("components", {0, panelHeight}, ImGuiChildFlags_Borders)) {
        if (current == entt::null) {
          TextDisabled("no entity selected");
        } else {
          components(r, current);
        }
      }
      EndChild();
      if (Button("New Entity")) {
        select(r, r.create());
      }
      SameLine();
      BeginDisabled(current == entt::null);
      if (Button("Duplicate")) {
        select(r, duplicate(r, current));
      }
      EndDisabled();
      SameLine();
      if (Button("Delete")) {
          r.destroy(current);
      }
    }
    End();
  }

  static void select(entt::registry &r, entt::entity e) {
    r.clear<Selected>();
    r.emplace<Selected>(e);
  }

  // Copies every copyable component. Renderable isn't (GPU buffers), and
  // ModelPlugin rebuilds it for any MeshRef that lacks one. The body is
  // re-created from its settings, so the copy gets its own instead of sharing
  // the BodyID. Shared_ptr components (MeshRef, MaterialRef) end up shared.
  static entt::entity duplicate(entt::registry &r, entt::entity src) {
    const entt::entity dst = r.create();
    for (auto [id, storage] : r.storage()) {
      // contains(dst): the entity pool itself already has it.
      if (!storage.contains(src) || storage.contains(dst)) {
        continue;
      }
      const auto hash = storage.type().hash();
      if (hash == entt::type_hash<Selected>::value() ||
          hash == entt::type_hash<JPH::BodyID>::value() ||
          hash == entt::type_hash<Relationship>::value()) {
        continue; // selection, owned body, hierarchy links: not the copy's
      }
      storage.push(dst, storage.value(src));
    }
    if (const auto *id = r.try_get<JPH::BodyID>(src)) {
      auto &system = Core::get()->physicsManager->system;
      JPH::BodyLockRead lock(system.GetBodyLockInterface(), *id);
      if (lock.Succeeded()) {
        r.emplace<JPH::BodyCreationSettings>(
            dst, lock.GetBody().GetBodyCreationSettings());
      }
    }
    return dst;
  }

  void tree(entt::registry &r) {
    using namespace ImGui;
    if (!TreeNodeEx("entities", ImGuiTreeNodeFlags_DefaultOpen))
      return;
    // The rows in display order, so a shift-click knows what lies between.
    std::vector<entt::entity> rows;
    entt::entity clicked = entt::null;
    for (auto e : r.view<entt::entity>()) {
      rows.push_back(e);
      auto flags = ImGuiTreeNodeFlags_Leaf |
                   ImGuiTreeNodeFlags_NoTreePushOnOpen |
                   ImGuiTreeNodeFlags_SpanAvailWidth;
      if (r.all_of<Selected>(e)) {
        flags |= ImGuiTreeNodeFlags_Selected;
      }
      TreeNodeEx(reinterpret_cast<void *>(
                     static_cast<uintptr_t>(entt::to_integral(e))),
                 flags, "%s", entityName(e).c_str());
      if (IsItemClicked()) {
        clicked = e;
      }
    }
    TreePop();
    if (clicked != entt::null) {
      click(r, rows, clicked);
    }
  }

  // Plain click: just this one. Ctrl: toggle it. Shift: everything from the
  // last plain/ctrl-clicked row to this one. `e` goes in last, so it is
  // the one the components panel shows (views iterate newest first).
  void click(entt::registry &r, const std::vector<entt::entity> &rows,
             entt::entity e) {
    const ImGuiIO &io = ImGui::GetIO();
    if (io.KeyShift && r.valid(anchor)) {
      auto from = std::ranges::find(rows, anchor);
      auto to = std::ranges::find(rows, e);
      if (from != rows.end()) {
        if (from > to) {
          std::swap(from, to);
        }
        r.clear<Selected>();
        for (auto it = from; it <= to; ++it) {
          if (*it != e) {
            r.emplace<Selected>(*it);
          }
        }
        r.emplace<Selected>(e);
        return;
      }
    }
    if (io.KeyCtrl) {
      if (r.remove<Selected>(e) == 0) {
        r.emplace<Selected>(e);
      }
    } else {
      select(r, e);
    }
    anchor = e;
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
      if (!type || !storage.contains(e))
        continue;
      PushID(static_cast<int>(id));
      Text("%s", typeName(type).c_str());
      SameLine();
      if (Button("remove"))
        type.invoke("remove"_hs, {}, entt::forward_as_meta(r), e);
      PopID();
    }
    Separator();
    if (Button("Add component"))
      OpenPopup("add component");
    if (BeginPopup("add component")) {
      for (auto [id, type] : entt::resolve()) {
        const auto *storage = r.storage(id);
        if (storage != nullptr && storage->contains(e))
          continue;
        if (Selectable(typeName(type).c_str())) {
          type.invoke("add"_hs, {}, entt::forward_as_meta(r), e);
          CloseCurrentPopup();
        }
      }
      EndPopup();
    }
  }
};
