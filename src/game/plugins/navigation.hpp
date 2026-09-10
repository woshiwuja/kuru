#pragma once
#include <Kuru.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <atomic>
#include <chrono>
#include <entt/entity/entity.hpp>
#include <future>
#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "mesh_registry.hpp"

using namespace KR;

// Un solo flag: qui non c'e' acqua, erba o porte, quindi ogni poly camminabile
// vale 1 e il filtro include 1. ponytail: la tassonomia SAMPLE_POLYAREA_* del
// demo serve quando i tipi di terreno costano diversamente.
constexpr unsigned short NAV_POLY_WALK = 1;

// Passi numerati dentro build(): il worker li conta, la UI ci fa la barra.
constexpr int NAV_BUILD_STEPS = 6;

// Geometria dell'overlay prima di finire sulla GPU. Si costruisce nel thread
// di build, si carica nel main: Mesh::upload registra un command buffer e lo
// invia sulla coda di Core, che non e' sincronizzata.
struct DebugGeom {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

// Tutto in unita' mondo (metri). I valori in voxel li ricava buildNavMesh
// dividendo per cellSize/cellHeight, come Sample_SoloMesh::handleBuild:
// mescolare le due unita' e' l'errore classico di Recast. Default del
// RecastDemo.
struct NavConfig {
  // Attenzione alla scala: la mappa e' 1000x, quindi a 0.3 il grid e'
  // 3333x3333 e il watershed ci mette minuti. Il pannello mostra quanto ha
  // impiegato l'ultimo build.
  float cellSize = 0.3f;
  float cellHeight = 0.2f;
  float agentHeight = 2.0f;
  float agentRadius = 0.5f;
  float agentMaxClimb = .9f;
  float agentMaxSlope = 50.0f;   // gradi
  float regionMinSize = 10.0f;
  float regionMergeSize = 20.0f;
  float edgeMaxLen = 12.0f;
  float edgeMaxError = 1.3f;
  int vertsPerPoly = 6;
  float detailSampleDist = 6.0f;
  float detailSampleMaxError = 1.0f;
  bool drawDebug = true;
  glm::vec3 debugColor = {0.0f, 0.6f, 1.0f};
  float debugAlpha = 0.4f;
  float debugOffsetY = 0.1f;
};

// Possiede i tre oggetti Detour. Non copiabile: una copia raddoppierebbe le
// free nel distruttore.
struct NavMap {
  dtNavMesh *mesh = nullptr;
  dtNavMeshQuery *query = nullptr;
  dtCrowd *crowd = nullptr;

  NavMap() = default;
  NavMap(const NavMap &) = delete;
  NavMap &operator=(const NavMap &) = delete;
  NavMap(NavMap &&o) noexcept : mesh(o.mesh), query(o.query), crowd(o.crowd) {
    o.mesh = nullptr;
    o.query = nullptr;
    o.crowd = nullptr;
  }
  // Lets loadNavMesh() build into a scratch NavMap and, only once every step
  // succeeds, swap it into the ctx-owned one - a failure partway through just
  // destroys the scratch copy instead of leaving the real one half-built.
  NavMap &operator=(NavMap &&o) noexcept {
    if (this != &o) {
      dtFreeCrowd(crowd);
      dtFreeNavMeshQuery(query);
      dtFreeNavMesh(mesh);
      mesh = o.mesh;
      query = o.query;
      crowd = o.crowd;
      o.mesh = nullptr;
      o.query = nullptr;
      o.crowd = nullptr;
    }
    return *this;
  }
  ~NavMap() {
    dtFreeCrowd(crowd);
    dtFreeNavMeshQuery(query);
    dtFreeNavMesh(mesh);
  }
  // I poly della navmesh come triangoli, gia' in coordinate mondo (flatten()
  // ci ha messo dentro la matrice della mappa): l'entita' che li porta va con
  // Transform identita'. Solo CPU, quindi si puo' chiamare dal worker.
  DebugGeom debugGeometry(glm::vec3 color, float yOffset) const {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    // Const: le getTile/getMaxTiles pubbliche sono quelle const.
    const dtNavMesh *navMesh = mesh;
    for (int i = 0; i < navMesh->getMaxTiles(); i++) {
      const dtMeshTile *tile = navMesh->getTile(i);
      if (!tile || !tile->header) continue;
      for (int j = 0; j < tile->header->polyCount; j++) {
        const dtPoly *poly = &tile->polys[j];
        // Le off-mesh connection sono due punti, non un poligono: niente da
        // triangolare.
        if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
        if (poly->vertCount < 3) continue;

        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int k = 0; k < poly->vertCount; k++) {
          const float *v = &tile->verts[poly->verts[k] * 3];
          // Normale in su: la pipeline di debug non la usa, ma il vertex
          // shader la normalizza comunque.
          vertices.push_back(Vertex{.pos = {v[0], v[1] + yOffset, v[2]},
                                    .color = color,
                                    .texCoord = {0.0f, 0.0f},
                                    .normal = {0.0f, 1.0f, 0.0f}});
        }
        // Ventaglio: i poly di Detour sono convessi per costruzione.
        for (int k = 1; k + 1 < poly->vertCount; k++) {
          indices.push_back(base);
          indices.push_back(base + k);
          indices.push_back(base + k + 1);
        }
      }
    }
    return {std::move(vertices), std::move(indices)};
  }
};

struct NavAgent {
  int idx = -1; // indice dtCrowd; -1 = non ancora inserito nella folla
  float maxSpeed = 3.5f;
  float maxAcceleration = 8.0f;
};

// Richiesta di destinazione: consumata e cancellata, come BodyCreationSettings
// in PhysicsPlugin. requestMoveTarget ogni frame azzererebbe il path calcolato.
struct NavDest {
  glm::vec3 pos{0.0f};
};

struct NavigationPlugin : public Plugin {
  void init(entt::registry &reg) override;
  // Creare e distruggere entita' renderizzabili avviene qui e solo qui: in
  // update() il command buffer del frame ha gia' i draw registrati dentro, e
  // liberare un buffer che nomina ancora significa device lost. La UI si
  // limita ad alzare le due richieste qui sotto.
  void start(entt::registry &reg) override;
  void update(entt::registry &reg) override;

  NavConfig config;

  // Entita' su cui si costruisce (la sceglie la UI) e overlay dei poly, che e'
  // di questo plugin: lo distrugge e lo ricrea a ogni build.
  entt::entity target = entt::null;
  entt::entity debugEntity = entt::null;

  // Esito dell'ultimo build, per il pannello. lastError vuoto = andato bene.
  std::string lastError;
  float lastBuildSeconds = 0.0f;
  int lastPolyCount = 0;

  // Quello che il worker consegna al main thread: nessun oggetto Vulkan e
  // nessun riferimento al registry, solo roba che si puo' spostare.
  struct BuildResult {
    NavMap nav;
    DebugGeom debug;
    int polyCount = 0;
  };
  bool rebuildRequested = false;
  bool overlayRequested = false;
  std::atomic<int> buildPhase{0};
  std::chrono::steady_clock::time_point buildStarted;
  // Ultimo membro di proposito. Il distruttore di un future di std::async
  // aspetta il worker, e i membri muoiono in ordine inverso di dichiarazione:
  // cosi' l'attesa avviene prima che buildPhase, che il worker sta scrivendo,
  // sparisca sotto di lui.
  std::future<BuildResult> pending;

  // Lancia il build su un thread e torna subito. Non lancia eccezioni: quella
  // del worker riemerge da poll() e finisce in lastError, cosi' la UI vive.
  void rebuild(entt::registry &reg);
  // Raccoglie il risultato quando e' pronto: qui, sul main thread, avvengono
  // l'upload dell'overlay e l'inserimento nel registry.
  void poll(entt::registry &reg);
  void spawnDebug(entt::registry &reg);
  void spawnDebug(entt::registry &reg, const DebugGeom &geom);
  void clearDebug(entt::registry &reg);
  void UI(entt::registry &reg);
};
