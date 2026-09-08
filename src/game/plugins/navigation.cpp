#include "navigation.hpp"

#include "../../lib/core/core.hpp"
#include "map.hpp"
#include "render.hpp"
#include "transform.hpp"

#include <DetourNavMeshBuilder.h>
#include <Recast.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

constexpr int MAX_AGENTS = 128; // come CrowdTool.h:70

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

// unique_ptr con le rcFree*: il build ha nove punti di uscita e liberarle a
// mano in ognuno e' il modo classico di perdere un heightfield su un errore.
template <typename T, void (*F)(T *)> struct RcOwn {
  std::unique_ptr<T, decltype(F)> p;
  RcOwn(T *raw) : p(raw, F) {
    if (!raw) throw std::runtime_error("Recast: allocazione fallita");
  }
  T *operator->() const { return p.get(); }
  T &operator*() const { return *p; }
};

void build(NavMap &nav, const NavConfig &c, const NavGeom &geom) {
  rcContext ctx;

  rcConfig cfg{};
  cfg.cs = c.cellSize;
  cfg.ch = c.cellHeight;
  cfg.walkableSlopeAngle = c.agentMaxSlope;
  // ceil per l'altezza (un agente alto 2.0 non passa in 1.9), floor per il
  // gradino (non promettere una salita che non c'e'), come nel sample.
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
    throw std::runtime_error("NavConfig::vertsPerPoly oltre DT_VERTS_PER_POLYGON");
  }

  // 1. voxelizzazione
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

  // 2. filtri: in quest'ordine, ognuno assume il precedente fatto
  rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
  rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
  rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

  // 3. heightfield compatto ed erosione del raggio dell'agente
  RcOwn<rcCompactHeightfield, rcFreeCompactHeightfield> chf(
      rcAllocCompactHeightfield());
  if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb,
                                 *solid, *chf)) {
    throw std::runtime_error("rcBuildCompactHeightfield fallita");
  }
  if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) {
    throw std::runtime_error("rcErodeWalkableArea fallita");
  }

  // 4. regioni watershed: piu' lento delle alternative monotone, ma da' le
  // regioni migliori. ponytail: il partizionamento del demo si sceglie da GUI,
  // qui e' fisso.
  if (!rcBuildDistanceField(&ctx, *chf)) {
    throw std::runtime_error("rcBuildDistanceField fallita");
  }
  if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
    throw std::runtime_error("rcBuildRegions fallita");
  }

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
  RcOwn<rcPolyMeshDetail, rcFreePolyMeshDetail> dmesh(rcAllocPolyMeshDetail());
  if (!rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist,
                             cfg.detailSampleMaxError, *dmesh)) {
    throw std::runtime_error("rcBuildPolyMeshDetail fallita");
  }

  // 6. da Recast a Detour. Senza questi flag il filtro di query scarta tutto e
  // ogni findNearestPoly torna 0.
  for (int i = 0; i < pmesh->npolys; ++i) {
    if (pmesh->areas[i] == RC_WALKABLE_AREA) pmesh->flags[i] = NAV_POLY_WALK;
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
    throw std::runtime_error("dtCreateNavMeshData fallita");
  }

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
  if (!nav.crowd || !nav.crowd->init(MAX_AGENTS, c.agentRadius, nav.mesh)) {
    throw std::runtime_error("dtCrowd::init fallita");
  }
  nav.crowd->getEditableFilter(0)->setIncludeFlags(NAV_POLY_WALK);
  // ponytail: i quattro preset di obstacle avoidance di CrowdTool.cpp:162-193
  // sono per confrontarli da GUI; dtCrowd::init installa gia' un default nello
  // slot 0, che e' quello che usano gli agenti qui.
}

} // namespace

void NavigationPlugin::init(entt::registry &reg) {
  // Registrato dopo MapPlugin, che e' chi crea l'entita' Map con il suo MeshRef.
  for (auto [e, meshRef, transform] : reg.view<Map, MeshRef, Transform>().each()) {
    auto &nav = reg.ctx().emplace<NavMap>();
    build(nav, config, flatten(*meshRef.mesh, transform.matrix()));
    return; // un solo navmesh: solo mesh, non tiled
  }
}

void NavigationPlugin::update(entt::registry &reg) {
  auto *nav = reg.ctx().find<NavMap>();
  if (nav == nullptr || nav->crowd == nullptr) return; // nessuna mappa caricata

  const float *extents = nav->crowd->getQueryExtents();
  const dtQueryFilter *filter = nav->crowd->getFilter(0);

  // Agenti non ancora inseriti: li aggiunge dalla posizione del Transform.
  for (auto [e, agent, t] : reg.view<NavAgent, Transform>().each()) {
    if (agent.idx >= 0) continue;
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
    agent.idx = nav->crowd->addAgent(&t.position.x, &ap);
  }

  // Destinazioni: una richiesta, consumata. Ripeterla ogni frame farebbe
  // ricalcolare il path da zero e l'agente non partirebbe mai.
  for (auto [e, agent, dest] : reg.view<NavAgent, NavDest>().each()) {
    if (agent.idx < 0) continue;
    dtPolyRef ref = 0;
    float nearest[3] = {0, 0, 0};
    // La destinazione va agganciata a un poly: un punto in aria o fuori dal
    // navmesh non e' un target valido per il crowd.
    if (dtStatusFailed(nav->query->findNearestPoly(&dest.pos.x, extents, filter,
                                                   &ref, nearest)) ||
        ref == 0) {
      continue;
    }
    nav->crowd->requestMoveTarget(agent.idx, ref, nearest);
    reg.erase<NavDest>(e);
  }

  nav->crowd->update(Core::get()->deltaTime, nullptr);

  // Il crowd possiede la posizione degli agenti, quindi sovrascrive Transform.
  for (auto [e, agent, t] : reg.view<NavAgent, Transform>().each()) {
    if (agent.idx < 0) continue;
    const dtCrowdAgent *a = nav->crowd->getAgent(agent.idx);
    if (a == nullptr || !a->active) continue;
    t.position = {a->npos[0], a->npos[1], a->npos[2]};
  }
}
