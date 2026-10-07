#include "controller.hpp"
#include "../map/map.hpp"
#include "../picking/picking.hpp"
#include "../render/render.hpp"
#include "../transform/transform.hpp"
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL // glm/gtx/quaternion.hpp
#endif
#include <glm/gtx/quaternion.hpp>
#include <limits>

// Spawning/despawning renderables only here: in update() the frame's
// command buffer already references them (see PhysicsPlugin::start).
void CharacterControllerPlugin::start(entt::registry &r) {
    auto &render = renderer(r);
    if (!cone) {
        cone = createCone();
    }
    for (const Hit &hit : pendingMarkers) {
        const entt::entity e = r.create();
        // The cone's tip is its origin and it opens up +Y: turn +Y onto the
        // face normal.
        // params.x = 2: the translucent debug pipeline, flat vertex colour
        // at alpha params.y.
        render.spawn(r, e, cone, getTexture(r, ""), {2.0f, markerAlpha, 0.0f, 0.0f},
                     Transform{.position = {hit.point.x, hit.point.y, hit.point.z},
                               .rotation = glm::rotation(glm::vec3(0, 1, 0),
                                                         hit.normal)});
        r.emplace<DebugMesh>(e);
        r.emplace<ClickMarker>(e);
    }
    pendingMarkers.clear();

    std::vector<entt::entity> expired;
    for (auto [e, marker] : r.view<ClickMarker>().each()) {
        if (marker.age >= markerLifetime) {
            expired.push_back(e);
        }
    }
    for (entt::entity e : expired) {
        render.despawn(r, e);
    }
}

void CharacterControllerPlugin::update(entt::registry &r) {
    const float dt = Core::get()->realDeltaTime();
    for (auto [e, marker] : r.view<ClickMarker>().each()) {
        marker.age += dt;
    }

    const auto &input = *Core::get()->eventManager;
    const auto &frame = r.ctx().get<FrameContext>();
    if (frame.uiCapturesMouse) {
        return;
    }
    for (const SDL_Event &event : input.events) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_RIGHT) {
            clickMap(r, frame, {event.button.x, event.button.y});
        }
    }

    if (!input.down(SDL_BUTTON_LEFT)) {
        return;
    }
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    for(auto [e,camera] : r.view<Camera, MainCamera>().each()){
        if (castRay(camera.position(), screenToWorld(frame, {mouseX, mouseY}))){};
    }
}

// ponytail: tests every triangle, fine for one click on one map; a BVH (or
// Jolt's CastRay + GetWorldSpaceSurfaceNormal) if maps get huge.
void CharacterControllerPlugin::clickMap(entt::registry &r,
                                         const FrameContext &frame,
                                         glm::vec2 pixel) {
    const glm::vec3 origin = glm::inverse(frame.view)[3];
    const glm::vec3 dir = screenToWorld(frame, pixel) - origin;
    for (auto [e, meshRef, t] : r.view<Map, MeshRef, Transform>().each()) {
        // Model space, as in PickingPlugin::pick: t is unchanged by the move.
        const glm::mat4 inv = glm::inverse(t.matrix());
        const glm::vec3 o = glm::vec3(inv * glm::vec4(origin, 1.0f));
        const glm::vec3 d = glm::vec3(inv * glm::vec4(dir, 0.0f));
        const Mesh &mesh = *meshRef.mesh;
        float bestT = std::numeric_limits<float>::max();
        glm::vec3 bestNormal{0.0f};
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const glm::vec3 &a = mesh.vertices[mesh.indices[i]].pos;
            const glm::vec3 &b = mesh.vertices[mesh.indices[i + 1]].pos;
            const glm::vec3 &c = mesh.vertices[mesh.indices[i + 2]].pos;
            const float hit = rayTriangle(o, d, a, b, c);
            if (hit >= 0.0f && hit < bestT) {
                bestT = hit;
                bestNormal = glm::cross(b - a, c - a);
            }
        }
        if (bestT == std::numeric_limits<float>::max()) {
            continue;
        }
        // Normals go to world space by the inverse transpose; then face the
        // camera, whichever way the triangle is wound.
        glm::vec3 normal =
            glm::normalize(glm::transpose(glm::mat3(inv)) * bestNormal);
        if (glm::dot(normal, dir) > 0.0f) {
            normal = -normal;
        }
        pendingMarkers.push_back({origin + bestT * dir, normal});
    }
}
