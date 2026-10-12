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

// Path a character is walking, from navigation's findPath; `next` is the
// corner it is heading for. Removed on arrival.
struct WalkPath {
    std::vector<glm::vec3> points;
    size_t next = 0;
};

// Right-clicking it walks the selected player characters up to it. What
// happens there is up to other plugins (see Talkable).
struct Interactable {};

// On a player character walking to `with` to interact with it; whoever
// handles that kind of interactable removes it.
struct InteractWith {
    entt::entity with = entt::null;
};

struct CharacterControllerPlugin : public Plugin{
    static constexpr float walkSpeed = 4.0f;      // m/s
    static constexpr float walkResponse = 8.0f;   // 1/s: how fast velocity is corrected
    static constexpr float cornerRadius = 0.5f;   // m, in XZ: "reached" a corner
    static constexpr float turnRate = 10.0f;      // 1/s: how fast it turns to face the way it walks
    // Standing still, friction holds it on slopes up to atan(friction) (45
    // deg at 1, against the map's 1); walking, none, or it would drag the walk.
    static constexpr float idleFriction = 1.0f;
    static constexpr float walkFriction = 0.0f;

    static constexpr float markerLifetime = 1.0f; // seconds
    static constexpr float markerAlpha = 0.5f;
    std::shared_ptr<Mesh> cone;
    struct Hit {
        glm::vec3 point, normal;
    };
    std::vector<Hit> pendingMarkers; // clicked in update(), spawned in start()

    CharacterControllerPlugin() {
        registerComponent<ClickMarker>();
        registerComponent<Interactable>();
    }
    void init(entt::registry &r) override {
    }
    void start(entt::registry &r) override;
    void update(entt::registry &r) override;
    // Pushes characters with a WalkPath along it with forces, turning them
    // to face the way they walk.
    void walk(entt::registry &r);
    // Queues a marker on the nearest map triangle under `pixel`, if any, and
    // sends the selected characters there.
    void clickMap(entt::registry &r, const FrameContext &frame, glm::vec2 pixel);
    // Sends the selected player characters to the Interactable `other`,
    // with an InteractWith.
    void interactWith(entt::registry &r, entt::entity other);
};
