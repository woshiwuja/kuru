#pragma once
#define JPH_DEBUG_RENDERER
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Renderer/DebugRenderer.h>
#include <optional>

namespace KR {

static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::uint NUM_LAYERS = 2;
static constexpr JPH::BroadPhaseLayer BD_NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer BD_MOVING(1);
static constexpr JPH::uint BD_NUM_LAYERS(2);

class DebugRenderer : public JPH::DebugRenderer{
        void DrawLine(JPH::RVec3Arg inFrom,JPH::RVec3Arg inTo, JPH::ColorArg inColor) {

        };
};
struct PhysicsManager {
  const JPH::uint maxBodies = 65535;
  const JPH::uint numBodyMutexes = 0; // 0 = let Jolt pick
  const JPH::uint maxBodyPairs = 65535;
  const JPH::uint maxContactConstraints = 10240;
  JPH::TempAllocatorImpl tempAllocator{10 * 1024 * 1024};
  JPH::JobSystemThreadPool jobSystem{JPH::cMaxPhysicsJobs,
                                     JPH::cMaxPhysicsBarriers};
  JPH::BroadPhaseLayerInterfaceTable bpLayers{NUM_LAYERS, BD_NUM_LAYERS};
  JPH::ObjectLayerPairFilterTable objectPairs{NUM_LAYERS};
  std::optional<JPH::ObjectVsBroadPhaseLayerFilterTable> objVsBpLayers;
  JPH::PhysicsSystem system;
  JPH::BodyInterface &bodies() { return system.GetBodyInterface(); }
  void init();
  void update();
  void stop();
};
} // namespace KR
