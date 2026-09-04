#include "heightmap.hpp"
#include "mesh_registry.hpp"
#include "plugins.hpp"
#include "../../lib/model/model.hpp"

void HeightmapPlugin::run(entt::registry &reg) {
  // A Map with no MeshRef has not been built yet. Collect first: emplacing into
  // a storage the view iterates would invalidate it.
  std::vector<entt::entity> pending;
  for (auto entity : reg.view<Map>(entt::exclude<MeshRef>)) {
    pending.push_back(entity);
  }

  for (auto entity : pending) {
    const Map &map = reg.get<Map>(entity);
    auto mesh = getMesh(reg, map.path, map.cellSize, map.heightScale).handle();
    reg.emplace<MeshRef>(entity, mesh);
    // x = 1 switches the shader to procedural terrain shading, y and z give it
    // the world height range this mesh actually spans.
    renderer(reg).attach(reg, entity, getTexture(reg, map.texture),
                         {1.0f, mesh->minY, mesh->maxY, 0.0f});
  }
}
