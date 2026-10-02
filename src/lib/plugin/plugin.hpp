#pragma once
#include <entt/entt.hpp>
#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <fstream>
#include <type_traits>
#include "../common/common.hpp" // SaveFile

namespace KR {

using Saver = entt::basic_snapshot<entt::registry>;
using Loader = entt::basic_snapshot_loader<entt::registry>;

struct SerialEntry {
  void (*save)(Saver &, SaveFile &);
  void (*load)(Loader &, SaveFile &);
};
inline std::vector<SerialEntry> &serialTypes() {
  static std::vector<SerialEntry> v;
  return v;
}

template <typename T> void metaSave(Saver &s, SaveFile &savefile) {
  s.template get<T>(savefile);
}
template <typename T> void metaLoad(Loader &l, SaveFile &savefile) {
  l.template get<T>(savefile);
}
// Reflects a component so the inspector can name, add and remove it without
// knowing the type. Registry members can't be attached directly: try_get and
// remove are overloaded or variadic, so &registry::foo<T> is ambiguous.
template <typename T> void metaAdd(entt::registry &r, entt::entity e) {
  r.emplace_or_replace<T>(e);
}
template <typename T> void metaRemove(entt::registry &r, entt::entity e) {
  r.remove<T>(e);
}

template <typename T> void registerComponent() {
  using namespace entt::literals;
  static bool once =
      (serialTypes().push_back({&metaSave<T>, &metaLoad<T>}), true);
  (void)once;
  entt::meta_factory<T>{}
      .template func<&metaAdd<T>>("add"_hs)
      .template func<&metaRemove<T>>("remove"_hs);
}

inline std::string typeName(const entt::meta_type &t) {
  return std::string{t.info().name()};
}

// A plugin is a set of components and systems. Concrete plugins live in
// src/game/plugins; this is only the interface Core runs them through.
//
// Core runs the three frame hooks as three passes over every plugin, not one
// pass calling all three: that is what lets a plugin open something in start()
// that the others use in update() and close it in end(). Within a pass the
// order is registration order.
struct Plugin {
  virtual ~Plugin() = default;
  Plugin() = default;
  virtual void init(entt::registry &reg) {}
  virtual void start(entt::registry &reg) {}
  virtual void update(entt::registry &reg) {}
  virtual void end(entt::registry &reg) {}
  virtual void save() {};
  virtual void load() {};
};
} // namespace KR
