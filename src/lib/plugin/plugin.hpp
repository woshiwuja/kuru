#pragma once
#include <entt/entt.hpp>
#include <exception>
#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include <thread>
#include <tracy/Tracy.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <type_traits>
#include "../common/common.hpp" // SaveFile
#include "entt/entity/fwd.hpp"

namespace KR {

using Saver = entt::basic_snapshot<entt::registry>;
using Loader = entt::basic_snapshot_loader<entt::registry>;

struct SerialEntry {
  entt::id_type id; // type_hash: tags the type's pool in the save file
  void (*save)(Saver &, SaveFile &);
  void (*load)(Loader &, SaveFile &);
};
// Defined in plugin.cpp, not inline: on Windows an inline function's static
// is one copy per module, so the exe (which registers) and Kuru.dll (which
// saves/loads) would each see their own list.
std::vector<SerialEntry> &serialTypes();

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
  if constexpr (std::is_trivially_copyable_v<T>) {
    static bool once =
        (serialTypes().push_back(
            {entt::type_hash<T>::value(), &metaSave<T>, &metaLoad<T>}), true);
    (void)once;
  }
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
  virtual void init(entt::registry &reg) {
      ZoneScoped;
  }
  virtual void start(entt::registry &reg) {}
  virtual void update(entt::registry &reg) {}
  virtual void end(entt::registry &reg) {}
  virtual void save(entt::registry &reg, SaveFile &file) {}
  virtual void load(entt::registry &reg, SaveFile &file) {}
};

template<typename Result> struct BackgroundJob {
    std::future<Result> result;
    bool busy() const {return result.valid();}
    template<typename Function> void launch(Function fn){
        result = std::async(std::launch::async, std::move(fn));
    }
    // The result once it's ready, nullopt before. Rethrows the worker's
    // exception; either way the job is idle again afterwards.
    std::optional<Result> poll(){
        if (!result.valid() || result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return std::nullopt;
        }
        return result.get();
    }
    void wait(){
        if (result.valid()) {
            result.wait();
        }
    }
};

// One job at a time on a worker thread. Polled in start(): that's the only
// hook where a result may create or destroy renderables (see
// NavigationPlugin), so an override of start() must call this one.
template<typename Result> struct BackgroundPlugin : public Plugin{
    BackgroundJob<Result> job;
    virtual void launchNext(entt::registry &reg) {}
    virtual void finished(entt::registry &reg, Result &result) {}
    virtual void failed(entt::registry &reg, const std::exception &e) {
        std::cout << e.what() << "\n";
    }
    void start(entt::registry &reg) override {
        try {
            if (std::optional<Result> result = job.poll()) {
                finished(reg, *result);
            }
        }
        catch(const std::exception &e) {
            failed(reg, e);
        }
        if (!job.busy()) {
            launchNext(reg);
        }
    }
};
} // namespace KR
