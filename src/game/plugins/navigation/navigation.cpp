#include "navigation.hpp"
#include <Kuru.h>
#include "../camera/camera.hpp"
#include "core/core.hpp"
#include "../map/map.hpp"
#include "plugin/plugin.hpp"
#include "../render/render.hpp"
#include "../transform/transform.hpp"
#include <DetourAlloc.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "../material/material.hpp"

using namespace KR;

namespace {


// Recast vuole xyz stretti e indici int. Vertex e' 44 byte con normale e uv in
// mezzo, quindi la geometria va ricompattata: una volta sola, al build.
struct NavGeom {
  std::vector<float> verts; // 3 float per vertice
  std::vector<int> tris;    // 3 indici per triangolo
  float bmin[3] = {0, 0, 0};
  float bmax[3] = {0, 0, 0};

  int vertCount() const { return static_cast<int>(verts.size() / 3); }
  int triCount() const { return static_cast<int>(tris.size() / 3); }
};

NavGeom flatten(const Mesh &mesh, const glm::mat4 &model) {
  NavGeom g;
  g.verts.reserve(mesh.vertices.size() * 3);
  for (const Vertex &v : mesh.vertices) {
    // In coordinate mondo: il navmesh e' unico e condiviso, i vertici del mesh
    // sono locali all'entita'.
    const glm::vec3 p = glm::vec3(model * glm::vec4(v.pos, 1.0f));
    g.verts.push_back(p.x);
    g.verts.push_back(p.y);
    g.verts.push_back(p.z);
  }
  g.tris.assign(mesh.indices.begin(), mesh.indices.end());
  rcCalcBounds(g.verts.data(), g.vertCount(), g.bmin, g.bmax);
  return g;
}

template <typename T, void (*F)(T *)> struct RcOwn {
  std::unique_ptr<T, decltype(F)> p;
  RcOwn(T *raw) : p(raw, F) {
    if (!raw)
      throw std::runtime_error("Recast: allocazione fallita");
  }
  T *operator->() const { return p.get(); }
  T &operator*() const { return *p; }
};

// Torna il numero di poligoni della navmesh costruita. `progress` conta i
// passi numerati qui sotto (NAV_BUILD_STEPS in tutto) per la barra della UI:
// gira sul thread di build, quindi e' atomico.
int build(NavMap &nav, const NavConfig &c, const NavGeom &geom,
          std::atomic<int> &progress) {
  const auto start = std::chrono::high_resolution_clock::now();
  rcContext ctx;

  rcConfig cfg{};
  const float spanY = geom.bmax[1] - geom.bmin[1];
  const float minCellHeight = spanY / RC_SPAN_MAX_HEIGHT;
  cfg.ch = std::max(c.cellHeight, minCellHeight);
  if (cfg.ch > c.cellHeight) {
    std::cout << "Navmesh: terrain is " << spanY << " units tall, past what "
              << "cellHeight " << c.cellHeight << " can address ("
              << RC_SPAN_MAX_HEIGHT * c.cellHeight << "); using " << cfg.ch
              << " instead\n";
  }
  constexpr int MAX_GRID_DIM = 8000;
  const float spanX = geom.bmax[0] - geom.bmin[0];
  const float spanZ = geom.bmax[2] - geom.bmin[2];
  const float minCellSize = std::max(spanX, spanZ) / MAX_GRID_DIM;
  cfg.cs = std::max(c.cellSize, minCellSize);
  cfg.walkableSlopeAngle = c.agentMaxSlope;
  cfg.walkableHeight = static_cast<int>(std::ceil(c.agentHeight / cfg.ch));
  cfg.walkableClimb = static_cast<int>(std::floor(c.agentMaxClimb / cfg.ch));
  cfg.walkableRadius = static_cast<int>(std::ceil(c.agentRadius / cfg.cs));
  cfg.maxEdgeLen = static_cast<int>(c.edgeMaxLen / cfg.cs);
  cfg.maxSimplificationError = c.edgeMaxError;
  cfg.minRegionArea = static_cast<int>(rcSqr(c.regionMinSize));
  cfg.mergeRegionArea = static_cast<int>(rcSqr(c.regionMergeSize));
  cfg.maxVertsPerPoly = c.vertsPerPoly;
  cfg.detailSampleDist =
      c.detailSampleDist < 0.9f ? 0 : cfg.cs * c.detailSampleDist;
  cfg.detailSampleMaxError = cfg.ch * c.detailSampleMaxError;
  rcVcopy(cfg.bmin, geom.bmin);
  rcVcopy(cfg.bmax, geom.bmax);
  rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

  if (cfg.maxVertsPerPoly > DT_VERTS_PER_POLYGON) {
    throw std::runtime_error(
        "NavConfig::vertsPerPoly oltre DT_VERTS_PER_POLYGON");
  }
  std::cout << "navmesh: voxel grid " << cfg.width << "x" << cfg.height
            << " (cs=" << cfg.cs << " ch=" << cfg.ch << ")\n";

  progress = 1;
  // voxel
  RcOwn<rcHeightfield, rcFreeHeightField> solid(rcAllocHeightfield());
  if (!rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin,
                           cfg.bmax, cfg.cs, cfg.ch)) {
    throw std::runtime_error("rcCreateHeightfield fallita");
  }

  std::vector<unsigned char> areas(geom.triCount(), 0);
  rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, geom.verts.data(),
                          geom.vertCount(), geom.tris.data(), geom.triCount(),
                          areas.data());
  if (!rcRasterizeTriangles(&ctx, geom.verts.data(), geom.vertCount(),
                            geom.tris.data(), areas.data(), geom.triCount(),
                            *solid, cfg.walkableClimb)) {
    throw std::runtime_error("rcRasterizeTriangles fallita");
  }

  progress = 2;
  // filter
  rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
  rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
  rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

  progress = 3;
  // heightfield
  RcOwn<rcCompactHeightfield, rcFreeCompactHeightfield> chf(
      rcAllocCompactHeightfield());
  if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb,
                                 *solid, *chf)) {
    throw std::runtime_error("rcBuildCompactHeightfield fallita");
  }
  if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) {
    throw std::runtime_error("rcErodeWalkableArea fallita");
  }

  progress = 4;
  // watershed
  if (!rcBuildDistanceField(&ctx, *chf)) {
    throw std::runtime_error("rcBuildDistanceField fallita");
  }
  if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
    throw std::runtime_error("rcBuildRegions fallita");
  }

  progress = 5;
  // 5. contorni -> poly mesh -> detail mesh
  RcOwn<rcContourSet, rcFreeContourSet> cset(rcAllocContourSet());
  if (!rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen,
                       *cset)) {
    throw std::runtime_error("rcBuildContours fallita");
  }
  RcOwn<rcPolyMesh, rcFreePolyMesh> pmesh(rcAllocPolyMesh());
  if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) {
    throw std::runtime_error("rcBuildPolyMesh fallita");
  }
  std::cout << "navmesh: poly mesh " << pmesh->nverts << " verts, "
            << pmesh->npolys << " polys\n";
  RcOwn<rcPolyMeshDetail, rcFreePolyMeshDetail> dmesh(rcAllocPolyMeshDetail());
  if (!rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist,
                             cfg.detailSampleMaxError, *dmesh)) {
    throw std::runtime_error("rcBuildPolyMeshDetail fallita");
  }
  std::cout << "navmesh: detail mesh " << dmesh->nverts << " verts, "
            << dmesh->ntris << " tris\n";

  progress = 6;
  for (int i = 0; i < pmesh->npolys; ++i) {
    if (pmesh->areas[i] == RC_WALKABLE_AREA)
      pmesh->flags[i] = NAV_POLY_WALK;
  }

  dtNavMeshCreateParams params{};
  params.verts = pmesh->verts;
  params.vertCount = pmesh->nverts;
  params.polys = pmesh->polys;
  params.polyAreas = pmesh->areas;
  params.polyFlags = pmesh->flags;
  params.polyCount = pmesh->npolys;
  params.nvp = pmesh->nvp;
  params.detailMeshes = dmesh->meshes;
  params.detailVerts = dmesh->verts;
  params.detailVertsCount = dmesh->nverts;
  params.detailTris = dmesh->tris;
  params.detailTriCount = dmesh->ntris;
  // Queste tre in unita' mondo, non voxel: Detour le usa per l'autolink delle
  // off-mesh connection e per il resto le riporta cosi' come sono.
  params.walkableHeight = c.agentHeight;
  params.walkableRadius = c.agentRadius;
  params.walkableClimb = c.agentMaxClimb;
  rcVcopy(params.bmin, pmesh->bmin);
  rcVcopy(params.bmax, pmesh->bmax);
  params.cs = cfg.cs;
  params.ch = cfg.ch;
  params.buildBvTree = true;

  unsigned char *navData = nullptr;
  int navDataSize = 0;
  if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
    // I suoi due modi di fallire si distinguono solo dai conteggi: nessun
    // triangolo camminabile (mesh capovolta, slope, erosione) o una tile sola
    // che non basta piu'.
    throw std::runtime_error(
        "dtCreateNavMeshData fallita: " + std::to_string(pmesh->nverts) +
        " verts, " + std::to_string(pmesh->npolys) + " polys");
  }
  std::cout << "navmesh: detour data " << navDataSize << " bytes\n";

  nav.mesh = dtAllocNavMesh();
  if (!nav.mesh) {
    dtFree(navData);
    throw std::runtime_error("dtAllocNavMesh fallita");
  }
  // DT_TILE_FREE_DATA: da qui navData e' della navmesh, non nostro.
  if (dtStatusFailed(nav.mesh->init(navData, navDataSize, DT_TILE_FREE_DATA))) {
    dtFree(navData);
    throw std::runtime_error("dtNavMesh::init fallita");
  }

  nav.query = dtAllocNavMeshQuery();
  if (!nav.query || dtStatusFailed(nav.query->init(nav.mesh, 2048))) {
    throw std::runtime_error("dtNavMeshQuery::init fallita");
  }

  nav.crowd = dtAllocCrowd();
  if (!nav.crowd || !nav.crowd->init(256, c.agentRadius, nav.mesh)) {
    throw std::runtime_error("dtCrowd::init fallita");
  }
  nav.crowd->getEditableFilter(0)->setIncludeFlags(NAV_POLY_WALK);

  const auto elapsed = std::chrono::duration<float, std::milli>(
      std::chrono::high_resolution_clock::now() - start);
  std::cout << "navmesh: build done in " << elapsed.count() << "ms\n";
  return pmesh->npolys;
}

std::string navmeshCachePath(const std::string &modelPath) {
  const size_t dot = modelPath.find_last_of('.');
  const std::string stem =
      dot == std::string::npos ? modelPath : modelPath.substr(0, dot);
  return stem + ".bin";
}

constexpr uint32_t NAVMESH_CACHE_MAGIC = 0x4B564E4B; // "KNVK"
constexpr uint32_t NAVMESH_CACHE_VERSION = 1;
struct NavMeshCacheHeader {
  uint32_t magic;
  uint32_t version;
  int32_t dataSize;
};

// The navmesh here is a single non-tiled mesh, so the whole thing is exactly
// the one blob dtCreateNavMeshData produced - getTile(0) hands that same
// blob back, nothing else to serialize.
void saveNavMesh(const dtNavMesh &mesh, const std::string &path) {
  const dtMeshTile *tile = mesh.getTile(0);
  if (tile == nullptr || tile->data == nullptr)
    return;

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    std::cout << "navmesh: could not open " << path << " for writing\n";
    return;
  }
  NavMeshCacheHeader header{NAVMESH_CACHE_MAGIC, NAVMESH_CACHE_VERSION,
                            tile->dataSize};
  file.write(reinterpret_cast<const char *>(&header), sizeof(header));
  file.write(reinterpret_cast<const char *>(tile->data), tile->dataSize);
  std::cout << "navmesh: saved " << tile->dataSize << " bytes to " << path
            << "\n";
}

bool loadNavMesh(NavMap &nav, const NavConfig &c, const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return false; // no cache yet, not an error

  NavMeshCacheHeader header{};
  file.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!file || header.magic != NAVMESH_CACHE_MAGIC ||
      header.version != NAVMESH_CACHE_VERSION || header.dataSize <= 0) {
    std::cout << "navmesh: " << path
              << " is missing/incompatible, rebuilding\n";
    return false;
  }

  auto *navData =
      static_cast<unsigned char *>(dtAlloc(header.dataSize, DT_ALLOC_PERM));
  if (navData == nullptr)
    return false;
  file.read(reinterpret_cast<char *>(navData), header.dataSize);
  if (!file) {
    dtFree(navData);
    std::cout << "navmesh: " << path << " is truncated, rebuilding\n";
    return false;
  }

  NavMap loaded;
  loaded.mesh = dtAllocNavMesh();
  // DT_TILE_FREE_DATA: on success navData belongs to the navmesh now; on
  // failure init() never took it, so it's still ours to free.
  if (loaded.mesh == nullptr ||
      dtStatusFailed(
          loaded.mesh->init(navData, header.dataSize, DT_TILE_FREE_DATA))) {
    dtFree(navData);
    return false;
  }
  loaded.query = dtAllocNavMeshQuery();
  if (loaded.query == nullptr ||
      dtStatusFailed(loaded.query->init(loaded.mesh, 2048))) {
    return false; // ~NavMap() cleans up loaded.mesh
  }
  loaded.crowd = dtAllocCrowd();
  if (loaded.crowd == nullptr ||
      !loaded.crowd->init(256, c.agentRadius, loaded.mesh)) {
    return false;
  }
  loaded.crowd->getEditableFilter(0)->setIncludeFlags(NAV_POLY_WALK);

  nav = std::move(loaded);
  std::cout << "navmesh: loaded from " << path << " (" << header.dataSize
            << " bytes)\n";
  return true;
}

} // namespace

std::vector<glm::vec3> findPath(const NavMap &nav, glm::vec3 from,
                                glm::vec3 to) {
  if (nav.query == nullptr) {
    return {};
  }
  constexpr int maxPolys = 256;
  // Generous in Y: `from` is a body's centre, not its feet.
  const float extents[3] = {2.0f, 4.0f, 2.0f};
  dtQueryFilter filter;
  filter.setIncludeFlags(NAV_POLY_WALK);
  dtPolyRef startRef = 0, endRef = 0;
  float start[3], end[3];
  if (dtStatusFailed(nav.query->findNearestPoly(&from.x, extents, &filter,
                                                &startRef, start)) ||
      startRef == 0 ||
      dtStatusFailed(nav.query->findNearestPoly(&to.x, extents, &filter,
                                                &endRef, end)) ||
      endRef == 0) {
    return {};
  }
  dtPolyRef polys[maxPolys];
  int polyCount = 0;
  if (dtStatusFailed(nav.query->findPath(startRef, endRef, start, end, &filter,
                                         polys, &polyCount, maxPolys)) ||
      polyCount == 0) {
    return {};
  }
  // Partial path (unreachable, or longer than maxPolys): stop on its last
  // poly instead of walking at a wall.
  if (polys[polyCount - 1] != endRef) {
    nav.query->closestPointOnPoly(polys[polyCount - 1], end, end, nullptr);
  }
  float corners[3 * maxPolys];
  int cornerCount = 0;
  if (dtStatusFailed(nav.query->findStraightPath(start, end, polys, polyCount,
                                                 corners, nullptr, nullptr,
                                                 &cornerCount, maxPolys))) {
    return {};
  }
  std::vector<glm::vec3> points;
  for (int i = 0; i < cornerCount; i++) {
    points.emplace_back(corners[3 * i], corners[3 * i + 1], corners[3 * i + 2]);
  }
  return points;
}

void NavigationPlugin::start(entt::registry &reg) {
  if (rebuildRequested) {
    rebuildRequested = false;
    // Every navmesh goes: drop them and the loop below queues them all again.
    // The old crowds die with them, so every agent index into them is stale:
    // update() reinserts the agents.
    for (auto [e, ref] : reg.view<NavMeshRef>().each()) {
      clearDebug(reg, ref);
    }
    reg.clear<NavMeshRef>();
    for (auto [e, agent] : reg.view<NavAgent>().each()) {
      agent.idx = -1;
    }
  }
  // Walkable entities without a navmesh, e.g. a map that just spawned. The
  // empty NavMeshRef keeps them out of this view while they wait.
  auto fresh = reg.view<Walkable, MeshRef, Transform>(entt::exclude<NavMeshRef>);
  const std::vector<entt::entity> found(fresh.begin(), fresh.end());
  for (entt::entity e : found) {
    reg.emplace<NavMeshRef>(e);
    queue.push_back(e);
  }
  poll(reg);
  buildNext(reg);
  if (overlayRequested) {
    overlayRequested = false;
    for (auto [e, ref] : reg.view<NavMeshRef>().each()) {
      if (config.drawDebug) {
        spawnDebug(reg, ref);
      } else {
        clearDebug(reg, ref);
      }
    }
  }
}

void NavigationPlugin::buildNext(entt::registry &reg) {
  if (pending.valid() || queue.empty()) {
    return; // uno alla volta
  }
  building = queue.front();
  queue.erase(queue.begin());
  lastError.clear();
  lastBuildSeconds = 0.0f;
  lastPolyCount = 0;
  if (!reg.valid(building) || !reg.all_of<MeshRef, Transform>(building)) {
    lastError = "walkable entity lost its mesh";
    return;
  }

  // Unica lettura del registry: da qui in poi il worker lavora su una copia
  // sua, e gli slider della UI possono muoversi senza entrare nel build.
  NavGeom geom = flatten(*reg.get<MeshRef>(building).mesh,
                         reg.get<Transform>(building).matrix());
  const NavConfig cfg = config;
  buildPhase = 0;
  buildStarted = std::chrono::steady_clock::now();
  pending = std::async(std::launch::async, [this, cfg, geom = std::move(geom)] {
    BuildResult out;
    out.polyCount = build(out.nav, cfg, geom, buildPhase);
    if (cfg.drawDebug) {
      out.debug = out.nav.debugGeometry(cfg.debugColor, cfg.debugOffsetY);
    }
    return out;
  });
}

void NavigationPlugin::poll(entt::registry &reg) {
  using namespace std::chrono_literals;
  if (!pending.valid() || pending.wait_for(0s) != std::future_status::ready) {
    return;
  }
  lastBuildSeconds = std::chrono::duration<float>(
                         std::chrono::steady_clock::now() - buildStarted)
                         .count();
  try {
    BuildResult out = pending.get();
    lastPolyCount = out.polyCount;
    // Destroyed (or rebuilt) while building: nowhere to put the result.
    if (!reg.valid(building) || !reg.all_of<NavMeshRef>(building)) {
      return;
    }
    auto &ref = reg.get<NavMeshRef>(building);
    ref.nav = std::move(out.nav);
    ref.polyCount = out.polyCount;
    spawnDebug(reg, ref, out.debug);
  } catch (const std::exception &e) {
    lastError = e.what();
  }
}

void NavigationPlugin::spawnDebug(entt::registry &reg, NavMeshRef &ref) {
  if (ref.nav.mesh == nullptr) {
    clearDebug(reg, ref);
    return;
  }
  spawnDebug(reg, ref,
             ref.nav.debugGeometry(config.debugColor, config.debugOffsetY));
}

void NavigationPlugin::spawnDebug(entt::registry &reg, NavMeshRef &ref,
                                  const DebugGeom &geom) {
  clearDebug(reg, ref);
  if (geom.indices.empty())
    return;

  auto mesh = std::make_shared<Mesh>();
  mesh->upload(geom.vertices, geom.indices);
  ref.debug = reg.create();
  renderer(reg).spawn(reg, ref.debug, std::move(mesh), getTexture(reg, ""),
                      {2.0f, config.debugAlpha, 0.0f, 0.0f});
  reg.emplace<DebugMesh>(ref.debug);
}

void NavigationPlugin::clearDebug(entt::registry &reg, NavMeshRef &ref) {
  if (reg.valid(ref.debug)) {
    renderer(reg).despawn(reg, ref.debug);
  }
  ref.debug = entt::null;
}

NavMeshRef *NavigationPlugin::navFor(entt::registry &reg, NavAgent &agent) {
  if (agent.walkable == entt::null) {
    for (auto [e, ref] : reg.view<NavMeshRef>().each()) {
      if (ref.nav.crowd != nullptr) {
        agent.walkable = e;
        break;
      }
    }
  }
  if (!reg.valid(agent.walkable)) {
    return nullptr;
  }
  auto *ref = reg.try_get<NavMeshRef>(agent.walkable);
  if (ref == nullptr || ref->nav.crowd == nullptr) {
    return nullptr;
  }
  return ref;
}

void NavigationPlugin::UI(entt::registry &reg) {
  using namespace ImGui;
  if (Begin("Navigation")) {
    SeparatorText("walkable");
    for (auto [e, ref] : reg.view<NavMeshRef>().each()) {
      if (ref.nav.mesh != nullptr) {
        Text("#%u: %d poly", entt::to_integral(e), ref.polyCount);
      } else {
        Text("#%u: not built", entt::to_integral(e));
      }
    }

    SeparatorText("agent");
    DragFloat("height", &config.agentHeight, 0.05f, 0.01f, 100.0f);
    DragFloat("radius", &config.agentRadius, 0.05f, 0.0f, 100.0f);
    DragFloat("max climb", &config.agentMaxClimb, 0.05f, 0.0f, 100.0f);
    SliderFloat("max slope", &config.agentMaxSlope, 0.0f, 90.0f);

    SeparatorText("voxel");
    DragFloat("cell size", &config.cellSize, 0.05f, 0.01f, 100.0f);
    DragFloat("cell height", &config.cellHeight, 0.05f, 0.01f, 100.0f);

    SeparatorText("regioni");
    DragFloat("min size", &config.regionMinSize, 0.5f, 0.0f, 1000.0f);
    DragFloat("merge size", &config.regionMergeSize, 0.5f, 0.0f, 1000.0f);

    SeparatorText("poligoni");
    DragFloat("max edge len", &config.edgeMaxLen, 0.5f, 0.0f, 1000.0f);
    DragFloat("max edge error", &config.edgeMaxError, 0.1f, 0.1f, 10.0f);
    SliderInt("verts per poly", &config.vertsPerPoly, 3, DT_VERTS_PER_POLYGON);
    DragFloat("detail sample dist", &config.detailSampleDist, 0.1f, 0.0f,
              100.0f);
    DragFloat("detail max error", &config.detailSampleMaxError, 0.1f, 0.0f,
              100.0f);

    SeparatorText("build");
    BeginDisabled(pending.valid() || !queue.empty() || rebuildRequested);
    if (Button("Rebuild all")) {
      rebuildRequested = true;
    }
    EndDisabled();
    SameLine();
    if (pending.valid()) {
      const int phase = buildPhase.load();
      ProgressBar(static_cast<float>(phase) / NAV_BUILD_STEPS, ImVec2(-1, 0),
                  std::format("{}/{}", phase, NAV_BUILD_STEPS).c_str());
    } else if (!lastError.empty()) {
      TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
    } else if (lastPolyCount > 0) {
      Text("%d poly in %.2f s", lastPolyCount, lastBuildSeconds);
    } else {
      TextUnformatted("mai costruita");
    }

    SeparatorText("debug");
    if (Checkbox("draw debug", &config.drawDebug)) {
      overlayRequested = true;
    }
    if (SliderFloat("alpha", &config.debugAlpha, 0.0f, 1.0f)) {
      for (auto [e, ref] : reg.view<NavMeshRef>().each()) {
        if (reg.valid(ref.debug) && reg.all_of<MaterialRef>(ref.debug)) {
          reg.get<MaterialRef>(ref.debug).material.get()->alphaCutoff =
              config.debugAlpha;
        }
      }
    }
    ColorEdit3("color", &config.debugColor.x);
    bool overlayDirty = IsItemDeactivatedAfterEdit();
    DragFloat("offset Y", &config.debugOffsetY, 0.1f);
    overlayDirty |= IsItemDeactivatedAfterEdit();
    if (overlayDirty && config.drawDebug) {
      overlayRequested = true;
    }
  }
  End();
}

void NavigationPlugin::update(entt::registry &reg) {
  UI(reg); // prima dell'uscita anticipata: il pannello serve soprattutto
           // quando la navmesh non c'e'.

  for (auto [e, agent, t] : reg.view<NavAgent, Transform>().each()) {
    if (agent.idx >= 0)
      continue;
    NavMeshRef *ref = navFor(reg, agent);
    if (ref == nullptr)
      continue;
    dtCrowdAgentParams ap{};
    ap.radius = config.agentRadius;
    ap.height = config.agentHeight;
    ap.maxAcceleration = agent.maxAcceleration;
    ap.maxSpeed = agent.maxSpeed;
    ap.collisionQueryRange = ap.radius * 12.0f;
    ap.pathOptimizationRange = ap.radius * 30.0f;
    ap.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS |
                     DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE |
                     DT_CROWD_SEPARATION;
    ap.separationWeight = 2.0f;
    // Detour is float-only; Transform::position is double for large-world range.
    const glm::vec3 pos = t.position;
    agent.idx = ref->nav.crowd->addAgent(&pos.x, &ap);
  }

  for (auto [e, agent, dest] : reg.view<NavAgent, NavDest>().each()) {
    if (agent.idx < 0)
      continue;
    NavMeshRef *ref = navFor(reg, agent);
    if (ref == nullptr)
      continue;
    const dtCrowd &crowd = *ref->nav.crowd;
    dtPolyRef poly = 0;
    float nearest[3] = {0, 0, 0};
    if (dtStatusFailed(ref->nav.query->findNearestPoly(
            &dest.pos.x, crowd.getQueryExtents(), crowd.getFilter(0), &poly,
            nearest)) ||
        poly == 0) {
      continue;
    }
    ref->nav.crowd->requestMoveTarget(agent.idx, poly, nearest);
    reg.erase<NavDest>(e);
  }

  for (auto [e, ref] : reg.view<NavMeshRef>().each()) {
    if (ref.nav.crowd != nullptr) {
      ref.nav.crowd->update(Core::get()->deltaTime(), nullptr);
    }
  }

  for (auto [e, agent, t] : reg.view<NavAgent, Transform>().each()) {
    if (agent.idx < 0)
      continue;
    NavMeshRef *ref = navFor(reg, agent);
    if (ref == nullptr)
      continue;
    const dtCrowdAgent *a = ref->nav.crowd->getAgent(agent.idx);
    if (a == nullptr || !a->active)
      continue;
  }
}
