#pragma once
#include "../camera/camera.hpp"
#include <Kuru.h>
#include "physics/raycast.hpp"
#include "entt/entity/fwd.hpp"
#include <memory>
#include <vector>

using namespace KR;

// The translucent cone a right-click on the map leaves behind, despawned
// after CharacterControllerPlugin::markerLifetime.
struct ClickMarker {
    float age = 0.0f;
};

struct CharacterControllerPlugin : public Plugin{
    static constexpr float markerLifetime = 1.0f; // seconds
    static constexpr float markerAlpha = 0.5f;
    std::shared_ptr<Mesh> cone;
    struct Hit {
        glm::vec3 point, normal;
    };
    std::vector<Hit> pendingMarkers; // clicked in update(), spawned in start()

    CharacterControllerPlugin() { registerComponent<ClickMarker>(); }
    void init(entt::registry &r) override {
    }
    void start(entt::registry &r) override;
    void update(entt::registry &r) override;
    // Queues a marker on the nearest map triangle under `pixel`, if any.
    void clickMap(entt::registry &r, const FrameContext &frame, glm::vec2 pixel);
};
