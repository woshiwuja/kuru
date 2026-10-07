#include "controller.hpp"
#include "character.hpp"
#include "../map/map.hpp"
#include "../navigation/navigation.hpp"
#include "../picking/picking.hpp"
#include "../render/render.hpp"
#include "../transform/transform.hpp"
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL // glm/gtx/quaternion.hpp
#endif
#include <glm/gtx/quaternion.hpp>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <cmath>
#include <limits>

// Spawning/despawning renderables only here: in update() the frame's
// command buffer already references them (see PhysicsPlugin::start).
void CharacterControllerPlugin::start(entt::registry &r) {
    // Characters stand upright and never turn by physics: every rotation is
    // locked (contacts and the walk force would spin them around Y), and the
    // body is created straightened, keeping its heading. Y translation stays
    // free so gravity keeps them on the ground. Before PhysicsPlugin::update
    // turns the settings into a body.
    for (auto [e, settings, t] :
         r.view<Character, JPH::BodyCreationSettings, Transform>().each()) {
        settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                                JPH::EAllowedDOFs::TranslationY |
                                JPH::EAllowedDOFs::TranslationZ;
        const glm::vec3 forward = t.rotation * glm::vec3(0, 0, 1);
        t.rotation = glm::angleAxis(std::atan2(forward.x, forward.z),
                                    glm::vec3(0, 1, 0));
    }

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

    walk(r);

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

void CharacterControllerPlugin::walk(entt::registry &r) {
    // Forces pile up on a body until the next step: none while paused.
    if (Core::get()->paused) {
        return;
    }
    auto &system = Core::get()->physicsManager->system;
    const float dt = Core::get()->deltaTime();
    std::vector<entt::entity> arrived;
    for (auto [e, path, t, id] : r.view<WalkPath, Transform, JPH::BodyID>().each()) {
        // Corners are on the ground and the body's origin isn't: compare in XZ.
        const glm::vec3 pos = t.position;
        glm::vec2 toCorner{0.0f};
        while (path.next < path.points.size()) {
            const glm::vec3 &corner = path.points[path.next];
            toCorner = {corner.x - pos.x, corner.z - pos.z};
            if (glm::length(toCorner) > cornerRadius) {
                break;
            }
            path.next++;
        }
        glm::vec2 wanted{0.0f};
        if (path.next < path.points.size()) {
            const float dist = glm::length(toCorner);
            float speed = walkSpeed;
            // Ease into the last corner instead of overshooting it.
            if (path.next + 1 == path.points.size()) {
                speed = std::min(walkSpeed, dist * walkResponse * 0.5f);
            }
            wanted = toCorner / dist * speed;
        } else {
            arrived.push_back(e);
        }

        // Horizontal force only: steer the XZ velocity toward `wanted` (zero
        // on arrival, as a brake) and leave Y to gravity and the ground.
        {
            JPH::BodyLockWrite lock(system.GetBodyLockInterface(), id);
            if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) {
                continue;
            }
            JPH::Body &body = lock.GetBody();
            const JPH::Vec3 v = body.GetLinearVelocity();
            const float mass = 1.0f / body.GetMotionProperties()->GetInverseMass();
            body.AddForce(JPH::Vec3(wanted.x - v.GetX(), 0.0f, wanted.y - v.GetZ()) *
                          (mass * walkResponse));
        }
        system.GetBodyInterface().ActivateBody(id);

        // Face the way it's walking: yaw only, eased so path corners don't
        // snap it round. Physics can't turn it (all rotation is locked), so
        // the body is set directly, and the Transform with it so this frame
        // draws it too. +Z is the model's front, as in start().
        if (glm::length(wanted) > 0.01f) {
            const glm::quat facing = glm::angleAxis(
                std::atan2(wanted.x, wanted.y), glm::vec3(0, 1, 0));
            t.rotation = glm::normalize(glm::slerp(
                t.rotation, facing, 1.0f - std::exp(-turnRate * dt)));
            system.GetBodyInterface().SetRotation(
                id, JPH::Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w),
                JPH::EActivation::DontActivate);
        }
    }
    for (entt::entity e : arrived) {
        r.remove<WalkPath>(e);
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
        const glm::vec3 point = origin + bestT * dir;
        pendingMarkers.push_back({point, normal});

        const auto *nav = r.try_get<NavMeshRef>(e);
        if (nav == nullptr) {
            continue;
        }
        for (auto [c, ct] : r.view<Character, Selected, Transform>().each()) {
            std::vector<glm::vec3> points = findPath(nav->nav, ct.position, point);
            if (!points.empty()) {
                r.emplace_or_replace<WalkPath>(c, std::move(points));
            }
        }
    }
}
