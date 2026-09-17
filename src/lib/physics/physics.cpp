#include "physics.hpp"
#include <iostream>
namespace KR {
    void PhysicsManager::init() {
      std::cerr << "[debug] PhysicsManager::init: new Factory\n" << std::flush;
      JPH::Factory::sInstance = new JPH::Factory();
      std::cerr << "[debug] PhysicsManager::init: RegisterTypes\n" << std::flush;
      JPH::RegisterTypes();

      std::cerr << "[debug] PhysicsManager::init: MapObjectToBroadPhaseLayer\n" << std::flush;
      bpLayers.MapObjectToBroadPhaseLayer(NON_MOVING, BD_NON_MOVING);
      bpLayers.MapObjectToBroadPhaseLayer(MOVING, BD_MOVING);
      std::cerr << "[debug] PhysicsManager::init: EnableCollision\n" << std::flush;
      objectPairs.EnableCollision(MOVING, NON_MOVING);
      objectPairs.EnableCollision(MOVING, MOVING);
      std::cerr << "[debug] PhysicsManager::init: objVsBpLayers.emplace\n" << std::flush;
      objVsBpLayers.emplace(bpLayers, BD_NUM_LAYERS, objectPairs,
                            NUM_LAYERS);
      std::cerr << "[debug] PhysicsManager::init: system.Init\n" << std::flush;
      system.Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints,
                  bpLayers, *objVsBpLayers, objectPairs);
      std::cerr << "[debug] PhysicsManager::init: done\n" << std::flush;
    }
    void PhysicsManager::update(float dt) {
          system.Update(dt, 1, &tempAllocator, &jobSystem);
        }
    void PhysicsManager::stop(){
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
    }
}
