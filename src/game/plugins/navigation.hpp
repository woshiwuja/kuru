#pragma once
#include "../../lib/plugin/plugin.hpp"
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>

// Un solo flag: qui non c'e' acqua, erba o porte, quindi ogni poly camminabile
// vale 1 e il filtro include 1. ponytail: la tassonomia SAMPLE_POLYAREA_* del
// demo serve quando i tipi di terreno costano diversamente.
constexpr unsigned short NAV_POLY_WALK = 1;

// Tutto in unita' mondo (metri). I valori in voxel li ricava buildNavMesh
// dividendo per cellSize/cellHeight, come Sample_SoloMesh::handleBuild:
// mescolare le due unita' e' l'errore classico di Recast. Default del RecastDemo.
struct NavConfig {
  float cellSize = 0.3f;
  float cellHeight = 0.2f;
  float agentHeight = 2.0f;
  float agentRadius = 0.6f;
  float agentMaxClimb = 0.9f;
  float agentMaxSlope = 45.0f;       // gradi
  float regionMinSize = 8.0f;        // lato: l'area e' il quadrato
  float regionMergeSize = 20.0f;     // idem
  float edgeMaxLen = 12.0f;
  float edgeMaxError = 1.3f;
  int vertsPerPoly = 6;
  float detailSampleDist = 6.0f;
  float detailSampleMaxError = 1.0f;
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
  NavMap(NavMap &&o) noexcept
      : mesh(o.mesh), query(o.query), crowd(o.crowd) {
    o.mesh = nullptr;
    o.query = nullptr;
    o.crowd = nullptr;
  }
  ~NavMap() {
    dtFreeCrowd(crowd);
    dtFreeNavMeshQuery(query);
    dtFreeNavMesh(mesh);
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
  void update(entt::registry &reg) override;

  NavConfig config;
};
