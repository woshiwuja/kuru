#pragma once

#include <entt/entt.hpp>
#include <memory>
#include <string>

#include "../../lib/common/common.hpp"
#include "../../lib/model/model.hpp"
#include "../../lib/plugin/plugin.hpp"

struct MeshLoader {
  using result_type = std::shared_ptr<Mesh>;

  result_type operator()(const std::string &path) const {
    return loadModel(path);
  }
};

using MeshCache = entt::resource_cache<Mesh, MeshLoader>;
struct MeshRegistryPlugin : public Plugin {
  void init(entt::registry &reg) override { reg.ctx().emplace<MeshCache>(); }
};

inline entt::resource<Mesh> getMesh(entt::registry &reg,
                                    const std::string &path) {
  auto &cache = reg.ctx().get<MeshCache>();
  return cache.load(entt::hashed_string::value(path.c_str()), path)
      .first->second;
}
